// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2011-2017, The Linux Foundation. All rights reserved.
// Copyright (c) 2018, Linaro Limited

#include <linux/init.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/bitops.h>
#include <linux/component.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/control.h>
#include <sound/asound.h>
#include <sound/pcm_params.h>
#include "q6afe.h"
#include "q6asm.h"
#include "q6adm.h"
#include "q6routing.h"

#define DRV_NAME "q6routing-component"

struct session_data {
	int state;
	int port_id;
	int path_type;
	int app_type;
	int acdb_id;
	int sample_rate;
	int bits_per_sample;
	int channels;
	int perf_mode;
	int numcopps;
	int fedai_id;
	unsigned long copp_map;
	struct q6copp *copps[MAX_COPPS_PER_PORT];
};

struct msm_routing_data {
	struct session_data sessions[MAX_SESSIONS];
	struct session_data port_data[AFE_MAX_PORTS];
	struct device *dev;
	struct mutex lock;
};

static struct msm_routing_data *routing_data;

/**
 * q6routing_stream_open() - Register a new stream for route setup
 *
 * @fedai_id: Frontend dai id.
 * @perf_mode: Performance mode.
 * @stream_id: ASM stream id to map.
 * @stream_type: Direction of stream
 *
 * Return: Will be an negative on error or a zero on success.
 */
int q6routing_stream_open(int fedai_id, int perf_mode,
			   int stream_id, int stream_type)
{
	int j, topology, num_copps = 0;
	struct route_payload payload;
	struct q6copp *copp;
	int copp_idx;
	struct session_data *session, *pdata;

	if (!routing_data) {
		pr_err("Routing driver not yet ready\n");
		return -EINVAL;
	}

	session = &routing_data->sessions[stream_id - 1];
	pdata = &routing_data->port_data[session->port_id];

	mutex_lock(&routing_data->lock);
	session->fedai_id = fedai_id;

	session->path_type = pdata->path_type;
	session->sample_rate = pdata->sample_rate;
	session->channels = pdata->channels;
	session->bits_per_sample = pdata->bits_per_sample;

	payload.num_copps = 0; /* only RX needs to use payload */
	topology = NULL_COPP_TOPOLOGY;
	copp = q6adm_open(routing_data->dev, session->port_id,
			      session->path_type, session->sample_rate,
			      session->channels, topology, perf_mode,
			      session->bits_per_sample, 0, 0);

	if (!copp) {
		mutex_unlock(&routing_data->lock);
		return -EINVAL;
	}

	copp_idx = q6adm_get_copp_id(copp);
	set_bit(copp_idx, &session->copp_map);
	session->copps[copp_idx] = copp;

	for_each_set_bit(j, &session->copp_map, MAX_COPPS_PER_PORT) {
		payload.port_id[num_copps] = session->port_id;
		payload.copp_idx[num_copps] = j;
		num_copps++;
	}

	if (num_copps) {
		payload.num_copps = num_copps;
		payload.session_id = stream_id;
		q6adm_matrix_map(routing_data->dev, session->path_type,
				 payload, perf_mode);
	}
	mutex_unlock(&routing_data->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(q6routing_stream_open);

static struct session_data *get_session_from_id(struct msm_routing_data *data,
						int fedai_id)
{
	int i;

	for (i = 0; i < MAX_SESSIONS; i++) {
		if (fedai_id == data->sessions[i].fedai_id)
			return &data->sessions[i];
	}

	return NULL;
}
/**
 * q6routing_stream_close() - Deregister a stream
 *
 * @fedai_id: Frontend dai id.
 * @stream_type: Direction of stream
 *
 * Return: Will be an negative on error or a zero on success.
 */
void q6routing_stream_close(int fedai_id, int stream_type)
{
	struct session_data *session;
	int idx;

	session = get_session_from_id(routing_data, fedai_id);
	if (!session)
		return;

	for_each_set_bit(idx, &session->copp_map, MAX_COPPS_PER_PORT) {
		if (session->copps[idx]) {
			q6adm_close(routing_data->dev, session->copps[idx]);
			session->copps[idx] = NULL;
		}
	}

	session->fedai_id = -1;
	session->copp_map = 0;
}
EXPORT_SYMBOL_GPL(q6routing_stream_close);

static int msm_routing_get_audio_mixer(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm =
	    snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct soc_mixer_control *mc =
	    (struct soc_mixer_control *)kcontrol->private_value;
	int session_id = mc->shift;
	struct snd_soc_component *c = snd_soc_dapm_to_component(dapm);
	struct msm_routing_data *priv = dev_get_drvdata(c->dev);
	struct session_data *session = &priv->sessions[session_id];

	if (session->port_id == mc->reg)
		ucontrol->value.integer.value[0] = 1;
	else
		ucontrol->value.integer.value[0] = 0;

	return 0;
}

static int msm_routing_put_audio_mixer(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm =
				    snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_component *c = snd_soc_dapm_to_component(dapm);
	struct msm_routing_data *data = dev_get_drvdata(c->dev);
	struct soc_mixer_control *mc =
		    (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_dapm_update *update = NULL;
	int be_id = mc->reg;
	int session_id = mc->shift;
	struct session_data *session = &data->sessions[session_id];

	if (ucontrol->value.integer.value[0]) {
		session->port_id = be_id;
		snd_soc_dapm_mixer_update_power(dapm, kcontrol, 1, update);
	} else {
		session->port_id = -1;
		snd_soc_dapm_mixer_update_power(dapm, kcontrol, 0, update);
	}

	return 1;
}

static const struct snd_kcontrol_new hdmi_mixer_controls[] = {
	SOC_SINGLE_EXT("MultiMedia1", HDMI_RX,
		       MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0,
		       msm_routing_get_audio_mixer,
		       msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia2", HDMI_RX,
		       MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0,
		       msm_routing_get_audio_mixer,
		       msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia3", HDMI_RX,
		       MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0,
		       msm_routing_get_audio_mixer,
		       msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia4", HDMI_RX,
		       MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0,
		       msm_routing_get_audio_mixer,
		       msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia5", HDMI_RX,
		       MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0,
		       msm_routing_get_audio_mixer,
		       msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia6", HDMI_RX,
		       MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0,
		       msm_routing_get_audio_mixer,
		       msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia7", HDMI_RX,
		       MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0,
		       msm_routing_get_audio_mixer,
		       msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia8", HDMI_RX,
		       MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0,
		       msm_routing_get_audio_mixer,
		       msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new primary_mi2s_rx_mixer_controls[] = {
	SOC_SINGLE_EXT("MultiMedia1", PRIMARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia2", PRIMARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia3", PRIMARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia4", PRIMARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia5", PRIMARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia6", PRIMARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia7", PRIMARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia8", PRIMARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new secondary_mi2s_rx_mixer_controls[] = {
	SOC_SINGLE_EXT("MultiMedia1", SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia2", SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia3", SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia4", SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia5", SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia6", SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia7", SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia8", SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new quaternary_mi2s_rx_mixer_controls[] = {
	SOC_SINGLE_EXT("MultiMedia1", QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia2", QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia3", QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia4", QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia5", QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia6", QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia7", QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia8", QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new tertiary_mi2s_rx_mixer_controls[] = {
	SOC_SINGLE_EXT("MultiMedia1", TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia2", TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia3", TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia4", TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};


static const struct snd_kcontrol_new slimbus_rx_mixer_controls[] = {
	SOC_SINGLE_EXT("MultiMedia1", SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia2", SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia3", SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia4", SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia5", SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia6", SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia7", SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia8", SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new slimbus_1_rx_mixer_controls[] = {
	SOC_SINGLE_EXT("MultiMedia1", SLIMBUS_1_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia2", SLIMBUS_1_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia3", SLIMBUS_1_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia4", SLIMBUS_1_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia5", SLIMBUS_1_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia6", SLIMBUS_1_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia7", SLIMBUS_1_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia8", SLIMBUS_1_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new slimbus_2_rx_mixer_controls[] = {
	SOC_SINGLE_EXT("MultiMedia1", SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia2", SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia3", SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia4", SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia5", SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia6", SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia7", SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia8", SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new slimbus_3_rx_mixer_controls[] = {
	SOC_SINGLE_EXT("MultiMedia1", SLIMBUS_3_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia2", SLIMBUS_3_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia3", SLIMBUS_3_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia4", SLIMBUS_3_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia5", SLIMBUS_3_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia6", SLIMBUS_3_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia7", SLIMBUS_3_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia8", SLIMBUS_3_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new slimbus_4_rx_mixer_controls[] = {
	SOC_SINGLE_EXT("MultiMedia1", SLIMBUS_4_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia2", SLIMBUS_4_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia5", SLIMBUS_4_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new slimbus_5_rx_mixer_controls[] = {
	SOC_SINGLE_EXT("MultiMedia1", SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia2", SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia3", SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia4", SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia5", SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia6", SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia7", SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia8", SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new slimbus_6_rx_mixer_controls[] = {
	SOC_SINGLE_EXT("MultiMedia1", SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia2", SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia3", SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia4", SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia5", SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia6", SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia7", SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("MultiMedia8", SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul1_mixer_controls[] = {
	SOC_SINGLE_EXT("PRI_MI2S_TX", PRIMARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("QUAT_MI2S_TX", QUATERNARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("TERT_MI2S_TX", TERTIARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("SEC_MI2S_TX", SECONDARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul2_mixer_controls[] = {
	SOC_SINGLE_EXT("PRI_MI2S_TX", PRIMARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("QUAT_MI2S_TX", QUATERNARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("TERT_MI2S_TX", TERTIARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("SEC_MI2S_TX", SECONDARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul3_mixer_controls[] = {
	SOC_SINGLE_EXT("PRI_MI2S_TX", PRIMARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("QUAT_MI2S_TX", QUATERNARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("TERT_MI2S_TX", TERTIARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("SEC_MI2S_TX", SECONDARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul4_mixer_controls[] = {
	SOC_SINGLE_EXT("PRI_MI2S_TX", PRIMARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("QUAT_MI2S_TX", QUATERNARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("TERT_MI2S_TX", TERTIARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("SEC_MI2S_TX", SECONDARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul5_mixer_controls[] = {
	SOC_SINGLE_EXT("PRI_MI2S_TX", PRIMARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("QUAT_MI2S_TX", QUATERNARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("TERT_MI2S_TX", TERTIARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("SEC_MI2S_TX", SECONDARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul6_mixer_controls[] = {
	SOC_SINGLE_EXT("PRI_MI2S_TX", PRIMARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("QUAT_MI2S_TX", QUATERNARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("TERT_MI2S_TX", TERTIARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("SEC_MI2S_TX", SECONDARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul7_mixer_controls[] = {
	SOC_SINGLE_EXT("PRI_MI2S_TX", PRIMARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("QUAT_MI2S_TX", QUATERNARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("TERT_MI2S_TX", TERTIARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("SEC_MI2S_TX", SECONDARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul8_mixer_controls[] = {
	SOC_SINGLE_EXT("PRI_MI2S_TX", PRIMARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("QUAT_MI2S_TX", QUATERNARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("TERT_MI2S_TX", TERTIARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("SEC_MI2S_TX", SECONDARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
};

static const struct snd_soc_dapm_widget msm_qdsp6_widgets[] = {
	/* Frontend AIF */
	SND_SOC_DAPM_AIF_IN("MM_DL1", "MultiMedia1 Playback", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("MM_DL2", "MultiMedia2 Playback", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("MM_DL3", "MultiMedia3 Playback", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("MM_DL4", "MultiMedia4 Playback", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("MM_DL5", "MultiMedia5 Playback", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("MM_DL6", "MultiMedia6 Playback", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("MM_DL7", "MultiMedia7 Playback", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("MM_DL8", "MultiMedia8 Playback", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("MM_UL1", "MultiMedia1 Capture", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("MM_UL2", "MultiMedia2 Capture", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("MM_UL3", "MultiMedia3 Capture", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("MM_UL4", "MultiMedia4 Capture", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("MM_UL5", "MultiMedia5 Capture", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("MM_UL6", "MultiMedia6 Capture", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("MM_UL7", "MultiMedia7 Capture", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("MM_UL8", "MultiMedia8 Capture", 0, 0, 0, 0),

	/* Mixer definitions */
	SND_SOC_DAPM_MIXER("HDMI Mixer", SND_SOC_NOPM, 0, 0,
			   hdmi_mixer_controls,
			   ARRAY_SIZE(hdmi_mixer_controls)),

	SND_SOC_DAPM_MIXER("SLIMBUS_0_RX Audio Mixer", SND_SOC_NOPM, 0, 0,
			   slimbus_rx_mixer_controls,
			   ARRAY_SIZE(slimbus_rx_mixer_controls)),
	SND_SOC_DAPM_MIXER("SLIMBUS_1_RX Audio Mixer", SND_SOC_NOPM, 0, 0,
			   slimbus_1_rx_mixer_controls,
			   ARRAY_SIZE(slimbus_1_rx_mixer_controls)),
	SND_SOC_DAPM_MIXER("SLIMBUS_2_RX Audio Mixer", SND_SOC_NOPM, 0, 0,
			   slimbus_2_rx_mixer_controls,
			   ARRAY_SIZE(slimbus_2_rx_mixer_controls)),
	SND_SOC_DAPM_MIXER("SLIMBUS_3_RX Audio Mixer", SND_SOC_NOPM, 0, 0,
			   slimbus_3_rx_mixer_controls,
			   ARRAY_SIZE(slimbus_3_rx_mixer_controls)),
	SND_SOC_DAPM_MIXER("SLIMBUS_4_RX Audio Mixer", SND_SOC_NOPM, 0, 0,
			   slimbus_4_rx_mixer_controls,
			   ARRAY_SIZE(slimbus_4_rx_mixer_controls)),
	SND_SOC_DAPM_MIXER("SLIMBUS_5_RX Audio Mixer", SND_SOC_NOPM, 0, 0,
			   slimbus_5_rx_mixer_controls,
			    ARRAY_SIZE(slimbus_5_rx_mixer_controls)),
	SND_SOC_DAPM_MIXER("SLIMBUS_6_RX Audio Mixer", SND_SOC_NOPM, 0, 0,
			   slimbus_6_rx_mixer_controls,
			   ARRAY_SIZE(slimbus_6_rx_mixer_controls)),
	SND_SOC_DAPM_MIXER("PRI_MI2S_RX Audio Mixer", SND_SOC_NOPM, 0, 0,
			   primary_mi2s_rx_mixer_controls,
			   ARRAY_SIZE(primary_mi2s_rx_mixer_controls)),
	SND_SOC_DAPM_MIXER("SEC_MI2S_RX Audio Mixer", SND_SOC_NOPM, 0, 0,
			   secondary_mi2s_rx_mixer_controls,
			   ARRAY_SIZE(secondary_mi2s_rx_mixer_controls)),
	SND_SOC_DAPM_MIXER("QUAT_MI2S_RX Audio Mixer", SND_SOC_NOPM, 0, 0,
			   quaternary_mi2s_rx_mixer_controls,
			   ARRAY_SIZE(quaternary_mi2s_rx_mixer_controls)),
	SND_SOC_DAPM_MIXER("TERT_MI2S_RX Audio Mixer", SND_SOC_NOPM, 0, 0,
			   tertiary_mi2s_rx_mixer_controls,
			   ARRAY_SIZE(tertiary_mi2s_rx_mixer_controls)),
	SND_SOC_DAPM_MIXER("MultiMedia1 Mixer", SND_SOC_NOPM, 0, 0,
		mmul1_mixer_controls, ARRAY_SIZE(mmul1_mixer_controls)),
	SND_SOC_DAPM_MIXER("MultiMedia2 Mixer", SND_SOC_NOPM, 0, 0,
		mmul2_mixer_controls, ARRAY_SIZE(mmul2_mixer_controls)),
	SND_SOC_DAPM_MIXER("MultiMedia3 Mixer", SND_SOC_NOPM, 0, 0,
		mmul3_mixer_controls, ARRAY_SIZE(mmul3_mixer_controls)),
	SND_SOC_DAPM_MIXER("MultiMedia4 Mixer", SND_SOC_NOPM, 0, 0,
		mmul4_mixer_controls, ARRAY_SIZE(mmul4_mixer_controls)),
	SND_SOC_DAPM_MIXER("MultiMedia5 Mixer", SND_SOC_NOPM, 0, 0,
		mmul5_mixer_controls, ARRAY_SIZE(mmul5_mixer_controls)),
	SND_SOC_DAPM_MIXER("MultiMedia6 Mixer", SND_SOC_NOPM, 0, 0,
		mmul6_mixer_controls, ARRAY_SIZE(mmul6_mixer_controls)),
	SND_SOC_DAPM_MIXER("MultiMedia7 Mixer", SND_SOC_NOPM, 0, 0,
		mmul7_mixer_controls, ARRAY_SIZE(mmul7_mixer_controls)),
	SND_SOC_DAPM_MIXER("MultiMedia8 Mixer", SND_SOC_NOPM, 0, 0,
		mmul8_mixer_controls, ARRAY_SIZE(mmul8_mixer_controls)),

};

static const struct snd_soc_dapm_route intercon[] = {
	{"HDMI Mixer", "MultiMedia1", "MM_DL1"},
	{"HDMI Mixer", "MultiMedia2", "MM_DL2"},
	{"HDMI Mixer", "MultiMedia3", "MM_DL3"},
	{"HDMI Mixer", "MultiMedia4", "MM_DL4"},
	{"HDMI Mixer", "MultiMedia5", "MM_DL5"},
	{"HDMI Mixer", "MultiMedia6", "MM_DL6"},
	{"HDMI Mixer", "MultiMedia7", "MM_DL7"},
	{"HDMI Mixer", "MultiMedia8", "MM_DL8"},
	{"HDMI_RX", NULL, "HDMI Mixer"},

	{"SLIMBUS_0_RX Audio Mixer", "MultiMedia1", "MM_DL1"},
	{"SLIMBUS_0_RX Audio Mixer", "MultiMedia2", "MM_DL2"},
	{"SLIMBUS_0_RX Audio Mixer", "MultiMedia3", "MM_DL3"},
	{"SLIMBUS_0_RX Audio Mixer", "MultiMedia4", "MM_DL4"},
	{"SLIMBUS_0_RX Audio Mixer", "MultiMedia5", "MM_DL5"},
	{"SLIMBUS_0_RX Audio Mixer", "MultiMedia6", "MM_DL6"},
	{"SLIMBUS_0_RX Audio Mixer", "MultiMedia7", "MM_DL7"},
	{"SLIMBUS_0_RX Audio Mixer", "MultiMedia8", "MM_DL8"},
	{"SLIMBUS_0_RX", NULL, "SLIMBUS_0_RX Audio Mixer"},

	{"SLIMBUS_1_RX Audio Mixer", "MultiMedia1", "MM_DL1"},
	{"SLIMBUS_1_RX Audio Mixer", "MultiMedia2", "MM_DL2"},
	{"SLIMBUS_1_RX Audio Mixer", "MultiMedia3", "MM_DL3"},
	{"SLIMBUS_1_RX Audio Mixer", "MultiMedia4", "MM_DL4"},
	{"SLIMBUS_1_RX Audio Mixer", "MultiMedia5", "MM_DL5"},
	{"SLIMBUS_1_RX Audio Mixer", "MultiMedia6", "MM_DL6"},
	{"SLIMBUS_1_RX Audio Mixer", "MultiMedia7", "MM_DL7"},
	{"SLIMBUS_1_RX Audio Mixer", "MultiMedia8", "MM_DL8"},
	{"SLIMBUS_1_RX", NULL, "SLIMBUS_1_RX Audio Mixer"},

	{"SLIMBUS_2_RX Audio Mixer", "MultiMedia1", "MM_DL1"},
	{"SLIMBUS_2_RX Audio Mixer", "MultiMedia2", "MM_DL2"},
	{"SLIMBUS_2_RX Audio Mixer", "MultiMedia3", "MM_DL3"},
	{"SLIMBUS_2_RX Audio Mixer", "MultiMedia4", "MM_DL4"},
	{"SLIMBUS_2_RX Audio Mixer", "MultiMedia5", "MM_DL5"},
	{"SLIMBUS_2_RX Audio Mixer", "MultiMedia6", "MM_DL6"},
	{"SLIMBUS_2_RX Audio Mixer", "MultiMedia7", "MM_DL7"},
	{"SLIMBUS_2_RX Audio Mixer", "MultiMedia8", "MM_DL8"},
	{"SLIMBUS_2_RX", NULL, "SLIMBUS_2_RX Audio Mixer"},

	{"SLIMBUS_3_RX Audio Mixer", "MultiMedia1", "MM_DL1"},
	{"SLIMBUS_3_RX Audio Mixer", "MultiMedia2", "MM_DL2"},
	{"SLIMBUS_3_RX Audio Mixer", "MultiMedia3", "MM_DL3"},
	{"SLIMBUS_3_RX Audio Mixer", "MultiMedia4", "MM_DL4"},
	{"SLIMBUS_3_RX Audio Mixer", "MultiMedia5", "MM_DL5"},
	{"SLIMBUS_3_RX Audio Mixer", "MultiMedia6", "MM_DL6"},
	{"SLIMBUS_3_RX Audio Mixer", "MultiMedia7", "MM_DL7"},
	{"SLIMBUS_3_RX Audio Mixer", "MultiMedia8", "MM_DL8"},
	{"SLIMBUS_3_RX", NULL, "SLIMBUS_3_RX Audio Mixer"},

	{"SLIMBUS_4_RX Audio Mixer", "MultiMedia1", "MM_DL1"},
	{"SLIMBUS_4_RX Audio Mixer", "MultiMedia2", "MM_DL2"},
	{"SLIMBUS_4_RX Audio Mixer", "MultiMedia5", "MM_DL5"},
	{"SLIMBUS_4_RX", NULL, "SLIMBUS_4_RX Audio Mixer"},

	{"SLIMBUS_5_RX Audio Mixer", "MultiMedia1", "MM_DL1"},
	{"SLIMBUS_5_RX Audio Mixer", "MultiMedia2", "MM_DL2"},
	{"SLIMBUS_5_RX Audio Mixer", "MultiMedia3", "MM_DL3"},
	{"SLIMBUS_5_RX Audio Mixer", "MultiMedia4", "MM_DL4"},
	{"SLIMBUS_5_RX Audio Mixer", "MultiMedia5", "MM_DL5"},
	{"SLIMBUS_5_RX Audio Mixer", "MultiMedia6", "MM_DL6"},
	{"SLIMBUS_5_RX Audio Mixer", "MultiMedia7", "MM_DL7"},
	{"SLIMBUS_5_RX Audio Mixer", "MultiMedia8", "MM_DL8"},
	{"SLIMBUS_5_RX", NULL, "SLIMBUS_5_RX Audio Mixer"},

	{"SLIMBUS_6_RX Audio Mixer", "MultiMedia1", "MM_DL1"},
	{"SLIMBUS_6_RX Audio Mixer", "MultiMedia2", "MM_DL2"},
	{"SLIMBUS_6_RX Audio Mixer", "MultiMedia3", "MM_DL3"},
	{"SLIMBUS_6_RX Audio Mixer", "MultiMedia4", "MM_DL4"},
	{"SLIMBUS_6_RX Audio Mixer", "MultiMedia5", "MM_DL5"},
	{"SLIMBUS_6_RX Audio Mixer", "MultiMedia6", "MM_DL6"},
	{"SLIMBUS_6_RX Audio Mixer", "MultiMedia7", "MM_DL7"},
	{"SLIMBUS_6_RX Audio Mixer", "MultiMedia8", "MM_DL8"},
	{"SLIMBUS_6_RX", NULL, "SLIMBUS_6_RX Audio Mixer"},

	{"QUAT_MI2S_RX Audio Mixer", "MultiMedia1", "MM_DL1"},
	{"QUAT_MI2S_RX Audio Mixer", "MultiMedia2", "MM_DL2"},
	{"QUAT_MI2S_RX Audio Mixer", "MultiMedia3", "MM_DL3"},
	{"QUAT_MI2S_RX Audio Mixer", "MultiMedia4", "MM_DL4"},
	{"QUAT_MI2S_RX Audio Mixer", "MultiMedia5", "MM_DL5"},
	{"QUAT_MI2S_RX Audio Mixer", "MultiMedia6", "MM_DL6"},
	{"QUAT_MI2S_RX Audio Mixer", "MultiMedia7", "MM_DL7"},
	{"QUAT_MI2S_RX Audio Mixer", "MultiMedia8", "MM_DL8"},
	{"QUAT_MI2S_RX", NULL, "QUAT_MI2S_RX Audio Mixer"},

	{"TERT_MI2S_RX Audio Mixer", "MultiMedia1", "MM_DL1"},
	{"TERT_MI2S_RX Audio Mixer", "MultiMedia2", "MM_DL2"},
	{"TERT_MI2S_RX Audio Mixer", "MultiMedia3", "MM_DL3"},
	{"TERT_MI2S_RX Audio Mixer", "MultiMedia4", "MM_DL4"},
	{"TERT_MI2S_RX", NULL, "TERT_MI2S_RX Audio Mixer"},

	{"SEC_MI2S_RX Audio Mixer", "MultiMedia1", "MM_DL1"},
	{"SEC_MI2S_RX Audio Mixer", "MultiMedia2", "MM_DL2"},
	{"SEC_MI2S_RX Audio Mixer", "MultiMedia3", "MM_DL3"},
	{"SEC_MI2S_RX Audio Mixer", "MultiMedia4", "MM_DL4"},
	{"SEC_MI2S_RX Audio Mixer", "MultiMedia5", "MM_DL5"},
	{"SEC_MI2S_RX Audio Mixer", "MultiMedia6", "MM_DL5"},
	{"SEC_MI2S_RX Audio Mixer", "MultiMedia7", "MM_DL7"},
	{"SEC_MI2S_RX Audio Mixer", "MultiMedia8", "MM_DL7"},
	{"SEC_MI2S_RX", NULL, "SEC_MI2S_RX Audio Mixer"},

	{"PRI_MI2S_RX Audio Mixer", "MultiMedia1", "MM_DL1"},
	{"PRI_MI2S_RX Audio Mixer", "MultiMedia2", "MM_DL2"},
	{"PRI_MI2S_RX Audio Mixer", "MultiMedia3", "MM_DL3"},
	{"PRI_MI2S_RX Audio Mixer", "MultiMedia4", "MM_DL4"},
	{"PRI_MI2S_RX Audio Mixer", "MultiMedia5", "MM_DL5"},
	{"PRI_MI2S_RX Audio Mixer", "MultiMedia7", "MM_DL7"},
	{"PRI_MI2S_RX", NULL, "PRI_MI2S_RX Audio Mixer"},

	{"MultiMedia1 Mixer", "PRI_MI2S_TX", "PRI_MI2S_TX"},
	{"MultiMedia1 Mixer", "SEC_MI2S_TX", "SEC_MI2S_TX"},
	{"MultiMedia1 Mixer", "QUAT_MI2S_TX", "QUAT_MI2S_TX"},
	{"MultiMedia1 Mixer", "TERT_MI2S_TX", "TERT_MI2S_TX"},

	{"MultiMedia2 Mixer", "PRI_MI2S_TX", "PRI_MI2S_TX"},
	{"MultiMedia2 Mixer", "SEC_MI2S_TX", "SEC_MI2S_TX"},
	{"MultiMedia2 Mixer", "QUAT_MI2S_TX", "QUAT_MI2S_TX"},
	{"MultiMedia2 Mixer", "TERT_MI2S_TX", "TERT_MI2S_TX"},

	{"MultiMedia3 Mixer", "PRI_MI2S_TX", "PRI_MI2S_TX"},
	{"MultiMedia3 Mixer", "SEC_MI2S_TX", "SEC_MI2S_TX"},
	{"MultiMedia3 Mixer", "QUAT_MI2S_TX", "QUAT_MI2S_TX"},
	{"MultiMedia3 Mixer", "TERT_MI2S_TX", "TERT_MI2S_TX"},

	{"MultiMedia4 Mixer", "PRI_MI2S_TX", "PRI_MI2S_TX"},
	{"MultiMedia4 Mixer", "SEC_MI2S_TX", "SEC_MI2S_TX"},
	{"MultiMedia4 Mixer", "QUAT_MI2S_TX", "QUAT_MI2S_TX"},
	{"MultiMedia4 Mixer", "TERT_MI2S_TX", "TERT_MI2S_TX"},

	{"MultiMedia5 Mixer", "PRI_MI2S_TX", "PRI_MI2S_TX"},
	{"MultiMedia5 Mixer", "SEC_MI2S_TX", "SEC_MI2S_TX"},
	{"MultiMedia5 Mixer", "QUAT_MI2S_TX", "QUAT_MI2S_TX"},
	{"MultiMedia5 Mixer", "TERT_MI2S_TX", "TERT_MI2S_TX"},

	{"MultiMedia6 Mixer", "PRI_MI2S_TX", "PRI_MI2S_TX"},
	{"MultiMedia6 Mixer", "SEC_MI2S_TX", "SEC_MI2S_TX"},
	{"MultiMedia6 Mixer", "QUAT_MI2S_TX", "QUAT_MI2S_TX"},
	{"MultiMedia6 Mixer", "TERT_MI2S_TX", "TERT_MI2S_TX"},

	{"MultiMedia7 Mixer", "PRI_MI2S_TX", "PRI_MI2S_TX"},
	{"MultiMedia7 Mixer", "SEC_MI2S_TX", "SEC_MI2S_TX"},
	{"MultiMedia7 Mixer", "QUAT_MI2S_TX", "QUAT_MI2S_TX"},
	{"MultiMedia7 Mixer", "TERT_MI2S_TX", "TERT_MI2S_TX"},

	{"MultiMedia8 Mixer", "PRI_MI2S_TX", "PRI_MI2S_TX"},
	{"MultiMedia8 Mixer", "SEC_MI2S_TX", "SEC_MI2S_TX"},
	{"MultiMedia8 Mixer", "QUAT_MI2S_TX", "QUAT_MI2S_TX"},
	{"MultiMedia8 Mixer", "TERT_MI2S_TX", "TERT_MI2S_TX"},

	{"MM_UL1", NULL, "MultiMedia1 Mixer"},
	{"MM_UL2", NULL, "MultiMedia2 Mixer"},
	{"MM_UL3", NULL, "MultiMedia3 Mixer"},
	{"MM_UL4", NULL, "MultiMedia4 Mixer"},
	{"MM_UL5", NULL, "MultiMedia5 Mixer"},
	{"MM_UL6", NULL, "MultiMedia6 Mixer"},
	{"MM_UL7", NULL, "MultiMedia7 Mixer"},
	{"MM_UL8", NULL, "MultiMedia8 Mixer"},
};

static int routing_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *c = snd_soc_rtdcom_lookup(rtd, DRV_NAME);
	struct msm_routing_data *data = dev_get_drvdata(c->dev);
	unsigned int be_id = rtd->cpu_dai->id;
	struct session_data *session;
	int path_type;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		path_type = ADM_PATH_PLAYBACK;
	else
		path_type = ADM_PATH_LIVE_REC;

	if (be_id > AFE_MAX_PORTS)
		return -EINVAL;

	session = &data->port_data[be_id];

	mutex_lock(&data->lock);

	session->path_type = path_type;
	session->sample_rate = params_rate(params);
	session->channels = params_channels(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
			session->bits_per_sample = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
			session->bits_per_sample = 24;
		break;
	default:
		break;
	}

	mutex_unlock(&data->lock);
	return 0;
}

static struct snd_pcm_ops q6pcm_routing_ops = {
	.hw_params = routing_hw_params,
};

static int msm_routing_probe(struct snd_soc_component *c)
{
	int i;

	for (i = 0; i < MAX_SESSIONS; i++)
		routing_data->sessions[i].port_id = -1;

	return 0;
}

static const struct snd_soc_component_driver msm_soc_routing_component = {
	.ops = &q6pcm_routing_ops,
	.probe = msm_routing_probe,
	.name = DRV_NAME,
	.dapm_widgets = msm_qdsp6_widgets,
	.num_dapm_widgets = ARRAY_SIZE(msm_qdsp6_widgets),
	.dapm_routes = intercon,
	.num_dapm_routes = ARRAY_SIZE(intercon),
};

static int q6routing_dai_bind(struct device *dev, struct device *master,
			      void *data)
{
	routing_data = kzalloc(sizeof(*routing_data), GFP_KERNEL);
	if (!routing_data)
		return -ENOMEM;

	routing_data->dev = dev;

	mutex_init(&routing_data->lock);
	dev_set_drvdata(dev, routing_data);

	return snd_soc_register_component(dev, &msm_soc_routing_component,
					  NULL, 0);
}

static void q6routing_dai_unbind(struct device *dev, struct device *master,
				 void *d)
{
	struct msm_routing_data *data = dev_get_drvdata(dev);

	snd_soc_unregister_component(dev);

	kfree(data);

	routing_data = NULL;
}

static const struct component_ops q6routing_dai_comp_ops = {
	.bind   = q6routing_dai_bind,
	.unbind = q6routing_dai_unbind,
};

static int q6pcm_routing_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &q6routing_dai_comp_ops);
}

static int q6pcm_routing_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &q6routing_dai_comp_ops);
	return 0;
}

static struct platform_driver q6pcm_routing_platform_driver = {
	.driver = {
		.name = "q6routing",
	},
	.probe = q6pcm_routing_probe,
	.remove = q6pcm_routing_remove,
};
module_platform_driver(q6pcm_routing_platform_driver);

MODULE_DESCRIPTION("Q6 Routing platform");
MODULE_LICENSE("GPL v2");
