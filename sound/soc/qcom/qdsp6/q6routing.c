// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2011-2017, The Linux Foundation. All rights reserved.
// Copyright (c) 2018, Linaro Limited

#include <linux/init.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/bitops.h>
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

#define Q6ROUTING_RX_MIXERS(id)						\
	SOC_SINGLE_EXT("MultiMedia1", id,				\
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,\
	msm_routing_put_audio_mixer),					\
	SOC_SINGLE_EXT("MultiMedia2", id,				\
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,\
	msm_routing_put_audio_mixer),					\
	SOC_SINGLE_EXT("MultiMedia3", id,				\
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,\
	msm_routing_put_audio_mixer),					\
	SOC_SINGLE_EXT("MultiMedia4", id,				\
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,\
	msm_routing_put_audio_mixer),					\
	SOC_SINGLE_EXT("MultiMedia5", id,				\
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,\
	msm_routing_put_audio_mixer),					\
	SOC_SINGLE_EXT("MultiMedia6", id,				\
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,\
	msm_routing_put_audio_mixer),					\
	SOC_SINGLE_EXT("MultiMedia7", id,				\
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,\
	msm_routing_put_audio_mixer),					\
	SOC_SINGLE_EXT("MultiMedia8", id,				\
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,\
	msm_routing_put_audio_mixer),

#define Q6ROUTING_RX_DAPM_ROUTE(mix_name, s)	\
	{ mix_name, "MultiMedia1", "MM_DL1" },	\
	{ mix_name, "MultiMedia2", "MM_DL2" },	\
	{ mix_name, "MultiMedia3", "MM_DL3" },	\
	{ mix_name, "MultiMedia4", "MM_DL4" },	\
	{ mix_name, "MultiMedia5", "MM_DL5" },	\
	{ mix_name, "MultiMedia6", "MM_DL6" },	\
	{ mix_name, "MultiMedia7", "MM_DL7" },	\
	{ mix_name, "MultiMedia8", "MM_DL8" },	\
	{ s, NULL, mix_name }

#define Q6ROUTING_TX_DAPM_ROUTE(mix_name)		\
	{ mix_name, "PRI_MI2S_TX", "PRI_MI2S_TX" },	\
	{ mix_name, "SEC_MI2S_TX", "SEC_MI2S_TX" },	\
	{ mix_name, "QUAT_MI2S_TX", "QUAT_MI2S_TX" },	\
	{ mix_name, "TERT_MI2S_TX", "TERT_MI2S_TX" },		\
	{ mix_name, "SLIMBUS_0_TX", "SLIMBUS_0_TX" },		\
	{ mix_name, "SLIMBUS_1_TX", "SLIMBUS_1_TX" },		\
	{ mix_name, "SLIMBUS_2_TX", "SLIMBUS_2_TX" },		\
	{ mix_name, "SLIMBUS_3_TX", "SLIMBUS_3_TX" },		\
	{ mix_name, "SLIMBUS_4_TX", "SLIMBUS_4_TX" },		\
	{ mix_name, "SLIMBUS_5_TX", "SLIMBUS_5_TX" },		\
	{ mix_name, "SLIMBUS_6_TX", "SLIMBUS_6_TX" },		\
	{ mix_name, "PRIMARY_TDM_TX_0", "PRIMARY_TDM_TX_0"},	\
	{ mix_name, "PRIMARY_TDM_TX_1", "PRIMARY_TDM_TX_1"},	\
	{ mix_name, "PRIMARY_TDM_TX_2", "PRIMARY_TDM_TX_2"},	\
	{ mix_name, "PRIMARY_TDM_TX_3", "PRIMARY_TDM_TX_3"},	\
	{ mix_name, "PRIMARY_TDM_TX_4", "PRIMARY_TDM_TX_4"},	\
	{ mix_name, "PRIMARY_TDM_TX_5", "PRIMARY_TDM_TX_5"},	\
	{ mix_name, "PRIMARY_TDM_TX_6", "PRIMARY_TDM_TX_6"},	\
	{ mix_name, "PRIMARY_TDM_TX_7", "PRIMARY_TDM_TX_7"},	\
	{ mix_name, "SEC_TDM_TX_0", "SEC_TDM_TX_0"},		\
	{ mix_name, "SEC_TDM_TX_1", "SEC_TDM_TX_1"},		\
	{ mix_name, "SEC_TDM_TX_2", "SEC_TDM_TX_2"},		\
	{ mix_name, "SEC_TDM_TX_3", "SEC_TDM_TX_3"},		\
	{ mix_name, "SEC_TDM_TX_4", "SEC_TDM_TX_4"},		\
	{ mix_name, "SEC_TDM_TX_5", "SEC_TDM_TX_5"},		\
	{ mix_name, "SEC_TDM_TX_6", "SEC_TDM_TX_6"},		\
	{ mix_name, "SEC_TDM_TX_7", "SEC_TDM_TX_7"},		\
	{ mix_name, "TERT_TDM_TX_0", "TERT_TDM_TX_0"},		\
	{ mix_name, "TERT_TDM_TX_1", "TERT_TDM_TX_1"},		\
	{ mix_name, "TERT_TDM_TX_2", "TERT_TDM_TX_2"},		\
	{ mix_name, "TERT_TDM_TX_3", "TERT_TDM_TX_3"},		\
	{ mix_name, "TERT_TDM_TX_4", "TERT_TDM_TX_4"},		\
	{ mix_name, "TERT_TDM_TX_5", "TERT_TDM_TX_5"},		\
	{ mix_name, "TERT_TDM_TX_6", "TERT_TDM_TX_6"},		\
	{ mix_name, "TERT_TDM_TX_7", "TERT_TDM_TX_7"},		\
	{ mix_name, "QUAT_TDM_TX_0", "QUAT_TDM_TX_0"},		\
	{ mix_name, "QUAT_TDM_TX_1", "QUAT_TDM_TX_1"},		\
	{ mix_name, "QUAT_TDM_TX_2", "QUAT_TDM_TX_2"},		\
	{ mix_name, "QUAT_TDM_TX_3", "QUAT_TDM_TX_3"},		\
	{ mix_name, "QUAT_TDM_TX_4", "QUAT_TDM_TX_4"},		\
	{ mix_name, "QUAT_TDM_TX_5", "QUAT_TDM_TX_5"},		\
	{ mix_name, "QUAT_TDM_TX_6", "QUAT_TDM_TX_6"},		\
	{ mix_name, "QUAT_TDM_TX_7", "QUAT_TDM_TX_7"},		\
	{ mix_name, "QUIN_TDM_TX_0", "QUIN_TDM_TX_0"},		\
	{ mix_name, "QUIN_TDM_TX_1", "QUIN_TDM_TX_1"},		\
	{ mix_name, "QUIN_TDM_TX_2", "QUIN_TDM_TX_2"},		\
	{ mix_name, "QUIN_TDM_TX_3", "QUIN_TDM_TX_3"},		\
	{ mix_name, "QUIN_TDM_TX_4", "QUIN_TDM_TX_4"},		\
	{ mix_name, "QUIN_TDM_TX_5", "QUIN_TDM_TX_5"},		\
	{ mix_name, "QUIN_TDM_TX_6", "QUIN_TDM_TX_6"},		\
	{ mix_name, "QUIN_TDM_TX_7", "QUIN_TDM_TX_7"}

#define Q6ROUTING_TX_MIXERS(id)						\
	SOC_SINGLE_EXT("PRI_MI2S_TX", PRIMARY_MI2S_TX,			\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("SEC_MI2S_TX", SECONDARY_MI2S_TX,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("TERT_MI2S_TX", TERTIARY_MI2S_TX,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("QUAT_MI2S_TX", QUATERNARY_MI2S_TX,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("SLIMBUS_0_TX", SLIMBUS_0_TX,			\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("SLIMBUS_1_TX", SLIMBUS_1_TX,			\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("SLIMBUS_2_TX", SLIMBUS_2_TX,			\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("SLIMBUS_3_TX", SLIMBUS_3_TX,			\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("SLIMBUS_4_TX", SLIMBUS_4_TX,			\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("SLIMBUS_5_TX", SLIMBUS_5_TX,			\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("SLIMBUS_6_TX", SLIMBUS_6_TX,			\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("PRIMARY_TDM_TX_0", PRIMARY_TDM_TX_0,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("PRIMARY_TDM_TX_1", PRIMARY_TDM_TX_1,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("PRIMARY_TDM_TX_2", PRIMARY_TDM_TX_2,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("PRIMARY_TDM_TX_3", PRIMARY_TDM_TX_3,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("PRIMARY_TDM_TX_4", PRIMARY_TDM_TX_4,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("PRIMARY_TDM_TX_5", PRIMARY_TDM_TX_5,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("PRIMARY_TDM_TX_6", PRIMARY_TDM_TX_6,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("PRIMARY_TDM_TX_7", PRIMARY_TDM_TX_7,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("SEC_TDM_TX_0", SECONDARY_TDM_TX_0,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("SEC_TDM_TX_1", SECONDARY_TDM_TX_1,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("SEC_TDM_TX_2", SECONDARY_TDM_TX_2,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("SEC_TDM_TX_3", SECONDARY_TDM_TX_3,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("SEC_TDM_TX_4", SECONDARY_TDM_TX_4,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("SEC_TDM_TX_5", SECONDARY_TDM_TX_5,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("SEC_TDM_TX_6", SECONDARY_TDM_TX_6,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("SEC_TDM_TX_7", SECONDARY_TDM_TX_7,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("TERT_TDM_TX_0", TERTIARY_TDM_TX_0,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("TERT_TDM_TX_1", TERTIARY_TDM_TX_1,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("TERT_TDM_TX_2", TERTIARY_TDM_TX_2,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("TERT_TDM_TX_3", TERTIARY_TDM_TX_3,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("TERT_TDM_TX_4", TERTIARY_TDM_TX_4,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("TERT_TDM_TX_5", TERTIARY_TDM_TX_5,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("TERT_TDM_TX_6", TERTIARY_TDM_TX_6,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("TERT_TDM_TX_7", TERTIARY_TDM_TX_7,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("QUAT_TDM_TX_0", QUATERNARY_TDM_TX_0,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("QUAT_TDM_TX_1", QUATERNARY_TDM_TX_1,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("QUAT_TDM_TX_2", QUATERNARY_TDM_TX_2,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("QUAT_TDM_TX_3", QUATERNARY_TDM_TX_3,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("QUAT_TDM_TX_4", QUATERNARY_TDM_TX_4,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("QUAT_TDM_TX_5", QUATERNARY_TDM_TX_5,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("QUAT_TDM_TX_6", QUATERNARY_TDM_TX_6,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("QUAT_TDM_TX_7", QUATERNARY_TDM_TX_7,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("QUIN_TDM_TX_0", QUINARY_TDM_TX_0,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("QUIN_TDM_TX_1", QUINARY_TDM_TX_1,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("QUIN_TDM_TX_2", QUINARY_TDM_TX_2,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("QUIN_TDM_TX_3", QUINARY_TDM_TX_3,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("QUIN_TDM_TX_4", QUINARY_TDM_TX_4,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("QUIN_TDM_TX_5", QUINARY_TDM_TX_5,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("QUIN_TDM_TX_6", QUINARY_TDM_TX_6,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),				\
	SOC_SINGLE_EXT("QUIN_TDM_TX_7", QUINARY_TDM_TX_7,		\
		id, 1, 0, msm_routing_get_audio_mixer,			\
		msm_routing_put_audio_mixer),

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

	if (IS_ERR_OR_NULL(copp)) {
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
	Q6ROUTING_RX_MIXERS(HDMI_RX) };

static const struct snd_kcontrol_new display_port_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(DISPLAY_PORT_RX) };

static const struct snd_kcontrol_new primary_mi2s_rx_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(PRIMARY_MI2S_RX) };

static const struct snd_kcontrol_new secondary_mi2s_rx_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(SECONDARY_MI2S_RX) };

static const struct snd_kcontrol_new quaternary_mi2s_rx_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(QUATERNARY_MI2S_RX) };

static const struct snd_kcontrol_new tertiary_mi2s_rx_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(TERTIARY_MI2S_RX) };

static const struct snd_kcontrol_new slimbus_rx_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(SLIMBUS_0_RX) };

static const struct snd_kcontrol_new slimbus_1_rx_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(SLIMBUS_1_RX) };

static const struct snd_kcontrol_new slimbus_2_rx_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(SLIMBUS_2_RX) };

static const struct snd_kcontrol_new slimbus_3_rx_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(SLIMBUS_3_RX) };

static const struct snd_kcontrol_new slimbus_4_rx_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(SLIMBUS_4_RX) };

static const struct snd_kcontrol_new slimbus_5_rx_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(SLIMBUS_5_RX) };

static const struct snd_kcontrol_new slimbus_6_rx_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(SLIMBUS_6_RX) };

static const struct snd_kcontrol_new pri_tdm_rx_0_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(PRIMARY_TDM_RX_0) };

static const struct snd_kcontrol_new pri_tdm_rx_1_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(PRIMARY_TDM_RX_1) };

static const struct snd_kcontrol_new pri_tdm_rx_2_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(PRIMARY_TDM_RX_2) };

static const struct snd_kcontrol_new pri_tdm_rx_3_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(PRIMARY_TDM_RX_3) };

static const struct snd_kcontrol_new pri_tdm_rx_4_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(PRIMARY_TDM_RX_4) };

static const struct snd_kcontrol_new pri_tdm_rx_5_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(PRIMARY_TDM_RX_5) };

static const struct snd_kcontrol_new pri_tdm_rx_6_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(PRIMARY_TDM_RX_6) };

static const struct snd_kcontrol_new pri_tdm_rx_7_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(PRIMARY_TDM_RX_7) };

static const struct snd_kcontrol_new sec_tdm_rx_0_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(SECONDARY_TDM_RX_0) };

static const struct snd_kcontrol_new sec_tdm_rx_1_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(SECONDARY_TDM_RX_1) };

static const struct snd_kcontrol_new sec_tdm_rx_2_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(SECONDARY_TDM_RX_2) };

static const struct snd_kcontrol_new sec_tdm_rx_3_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(SECONDARY_TDM_RX_3) };

static const struct snd_kcontrol_new sec_tdm_rx_4_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(SECONDARY_TDM_RX_4) };

static const struct snd_kcontrol_new sec_tdm_rx_5_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(SECONDARY_TDM_RX_5) };

static const struct snd_kcontrol_new sec_tdm_rx_6_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(SECONDARY_TDM_RX_6) };

static const struct snd_kcontrol_new sec_tdm_rx_7_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(SECONDARY_TDM_RX_7) };

static const struct snd_kcontrol_new tert_tdm_rx_0_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(TERTIARY_TDM_RX_0) };

static const struct snd_kcontrol_new tert_tdm_rx_1_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(TERTIARY_TDM_RX_1) };

static const struct snd_kcontrol_new tert_tdm_rx_2_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(TERTIARY_TDM_RX_2) };

static const struct snd_kcontrol_new tert_tdm_rx_3_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(TERTIARY_TDM_RX_3) };

static const struct snd_kcontrol_new tert_tdm_rx_4_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(TERTIARY_TDM_RX_4) };

static const struct snd_kcontrol_new tert_tdm_rx_5_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(TERTIARY_TDM_RX_5) };

static const struct snd_kcontrol_new tert_tdm_rx_6_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(TERTIARY_TDM_RX_6) };

static const struct snd_kcontrol_new tert_tdm_rx_7_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(TERTIARY_TDM_RX_7) };

static const struct snd_kcontrol_new quat_tdm_rx_0_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(QUATERNARY_TDM_RX_0) };

static const struct snd_kcontrol_new quat_tdm_rx_1_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(QUATERNARY_TDM_RX_1) };

static const struct snd_kcontrol_new quat_tdm_rx_2_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(QUATERNARY_TDM_RX_2) };

static const struct snd_kcontrol_new quat_tdm_rx_3_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(QUATERNARY_TDM_RX_3) };

static const struct snd_kcontrol_new quat_tdm_rx_4_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(QUATERNARY_TDM_RX_4) };

static const struct snd_kcontrol_new quat_tdm_rx_5_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(QUATERNARY_TDM_RX_5) };

static const struct snd_kcontrol_new quat_tdm_rx_6_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(QUATERNARY_TDM_RX_6) };

static const struct snd_kcontrol_new quat_tdm_rx_7_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(QUATERNARY_TDM_RX_7) };

static const struct snd_kcontrol_new quin_tdm_rx_0_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(QUINARY_TDM_RX_0) };

static const struct snd_kcontrol_new quin_tdm_rx_1_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(QUINARY_TDM_RX_1) };

static const struct snd_kcontrol_new quin_tdm_rx_2_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(QUINARY_TDM_RX_2) };

static const struct snd_kcontrol_new quin_tdm_rx_3_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(QUINARY_TDM_RX_3) };

static const struct snd_kcontrol_new quin_tdm_rx_4_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(QUINARY_TDM_RX_4) };

static const struct snd_kcontrol_new quin_tdm_rx_5_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(QUINARY_TDM_RX_5) };

static const struct snd_kcontrol_new quin_tdm_rx_6_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(QUINARY_TDM_RX_6) };

static const struct snd_kcontrol_new quin_tdm_rx_7_mixer_controls[] = {
	Q6ROUTING_RX_MIXERS(QUINARY_TDM_RX_7) };


static const struct snd_kcontrol_new mmul1_mixer_controls[] = {
	Q6ROUTING_TX_MIXERS(MSM_FRONTEND_DAI_MULTIMEDIA1) };

static const struct snd_kcontrol_new mmul2_mixer_controls[] = {
	Q6ROUTING_TX_MIXERS(MSM_FRONTEND_DAI_MULTIMEDIA2) };

static const struct snd_kcontrol_new mmul3_mixer_controls[] = {
	Q6ROUTING_TX_MIXERS(MSM_FRONTEND_DAI_MULTIMEDIA3) };

static const struct snd_kcontrol_new mmul4_mixer_controls[] = {
	Q6ROUTING_TX_MIXERS(MSM_FRONTEND_DAI_MULTIMEDIA4) };

static const struct snd_kcontrol_new mmul5_mixer_controls[] = {
	Q6ROUTING_TX_MIXERS(MSM_FRONTEND_DAI_MULTIMEDIA5) };

static const struct snd_kcontrol_new mmul6_mixer_controls[] = {
	Q6ROUTING_TX_MIXERS(MSM_FRONTEND_DAI_MULTIMEDIA6) };

static const struct snd_kcontrol_new mmul7_mixer_controls[] = {
	Q6ROUTING_TX_MIXERS(MSM_FRONTEND_DAI_MULTIMEDIA7) };

static const struct snd_kcontrol_new mmul8_mixer_controls[] = {
	Q6ROUTING_TX_MIXERS(MSM_FRONTEND_DAI_MULTIMEDIA8) };

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

	SND_SOC_DAPM_MIXER("DISPLAY_PORT_RX Audio Mixer", SND_SOC_NOPM, 0, 0,
			   display_port_mixer_controls,
			   ARRAY_SIZE(display_port_mixer_controls)),

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
	SND_SOC_DAPM_MIXER("PRIMARY_TDM_RX_0 Audio Mixer", SND_SOC_NOPM, 0, 0,
				pri_tdm_rx_0_mixer_controls,
				ARRAY_SIZE(pri_tdm_rx_0_mixer_controls)),
	SND_SOC_DAPM_MIXER("PRIMARY_TDM_RX_1 Audio Mixer", SND_SOC_NOPM, 0, 0,
				pri_tdm_rx_1_mixer_controls,
				ARRAY_SIZE(pri_tdm_rx_1_mixer_controls)),
	SND_SOC_DAPM_MIXER("PRIMARY_TDM_RX_2 Audio Mixer", SND_SOC_NOPM, 0, 0,
				pri_tdm_rx_2_mixer_controls,
				ARRAY_SIZE(pri_tdm_rx_2_mixer_controls)),
	SND_SOC_DAPM_MIXER("PRIMARY_TDM_RX_3 Audio Mixer", SND_SOC_NOPM, 0, 0,
				pri_tdm_rx_3_mixer_controls,
				ARRAY_SIZE(pri_tdm_rx_3_mixer_controls)),
	SND_SOC_DAPM_MIXER("PRIMARY_TDM_RX_4 Audio Mixer", SND_SOC_NOPM, 0, 0,
				pri_tdm_rx_4_mixer_controls,
				ARRAY_SIZE(pri_tdm_rx_4_mixer_controls)),
	SND_SOC_DAPM_MIXER("PRIMARY_TDM_RX_5 Audio Mixer", SND_SOC_NOPM, 0, 0,
				pri_tdm_rx_5_mixer_controls,
				ARRAY_SIZE(pri_tdm_rx_5_mixer_controls)),
	SND_SOC_DAPM_MIXER("PRIMARY_TDM_RX_6 Audio Mixer", SND_SOC_NOPM, 0, 0,
				pri_tdm_rx_6_mixer_controls,
				ARRAY_SIZE(pri_tdm_rx_6_mixer_controls)),
	SND_SOC_DAPM_MIXER("PRIMARY_TDM_RX_7 Audio Mixer", SND_SOC_NOPM, 0, 0,
				pri_tdm_rx_7_mixer_controls,
				ARRAY_SIZE(pri_tdm_rx_7_mixer_controls)),

	SND_SOC_DAPM_MIXER("SEC_TDM_RX_0 Audio Mixer", SND_SOC_NOPM, 0, 0,
				sec_tdm_rx_0_mixer_controls,
				ARRAY_SIZE(sec_tdm_rx_0_mixer_controls)),
	SND_SOC_DAPM_MIXER("SEC_TDM_RX_1 Audio Mixer", SND_SOC_NOPM, 0, 0,
				sec_tdm_rx_1_mixer_controls,
				ARRAY_SIZE(sec_tdm_rx_1_mixer_controls)),
	SND_SOC_DAPM_MIXER("SEC_TDM_RX_2 Audio Mixer", SND_SOC_NOPM, 0, 0,
				sec_tdm_rx_2_mixer_controls,
				ARRAY_SIZE(sec_tdm_rx_2_mixer_controls)),
	SND_SOC_DAPM_MIXER("SEC_TDM_RX_3 Audio Mixer", SND_SOC_NOPM, 0, 0,
				sec_tdm_rx_3_mixer_controls,
				ARRAY_SIZE(sec_tdm_rx_3_mixer_controls)),
	SND_SOC_DAPM_MIXER("SEC_TDM_RX_4 Audio Mixer", SND_SOC_NOPM, 0, 0,
				sec_tdm_rx_4_mixer_controls,
				ARRAY_SIZE(sec_tdm_rx_4_mixer_controls)),
	SND_SOC_DAPM_MIXER("SEC_TDM_RX_5 Audio Mixer", SND_SOC_NOPM, 0, 0,
				sec_tdm_rx_5_mixer_controls,
				ARRAY_SIZE(sec_tdm_rx_5_mixer_controls)),
	SND_SOC_DAPM_MIXER("SEC_TDM_RX_6 Audio Mixer", SND_SOC_NOPM, 0, 0,
				sec_tdm_rx_6_mixer_controls,
				ARRAY_SIZE(sec_tdm_rx_6_mixer_controls)),
	SND_SOC_DAPM_MIXER("SEC_TDM_RX_7 Audio Mixer", SND_SOC_NOPM, 0, 0,
				sec_tdm_rx_7_mixer_controls,
				ARRAY_SIZE(sec_tdm_rx_7_mixer_controls)),

	SND_SOC_DAPM_MIXER("TERT_TDM_RX_0 Audio Mixer", SND_SOC_NOPM, 0, 0,
				tert_tdm_rx_0_mixer_controls,
				ARRAY_SIZE(tert_tdm_rx_0_mixer_controls)),
	SND_SOC_DAPM_MIXER("TERT_TDM_RX_1 Audio Mixer", SND_SOC_NOPM, 0, 0,
				tert_tdm_rx_1_mixer_controls,
				ARRAY_SIZE(tert_tdm_rx_1_mixer_controls)),
	SND_SOC_DAPM_MIXER("TERT_TDM_RX_2 Audio Mixer", SND_SOC_NOPM, 0, 0,
				tert_tdm_rx_2_mixer_controls,
				ARRAY_SIZE(tert_tdm_rx_2_mixer_controls)),
	SND_SOC_DAPM_MIXER("TERT_TDM_RX_3 Audio Mixer", SND_SOC_NOPM, 0, 0,
				tert_tdm_rx_3_mixer_controls,
				ARRAY_SIZE(tert_tdm_rx_3_mixer_controls)),
	SND_SOC_DAPM_MIXER("TERT_TDM_RX_4 Audio Mixer", SND_SOC_NOPM, 0, 0,
				tert_tdm_rx_4_mixer_controls,
				ARRAY_SIZE(tert_tdm_rx_4_mixer_controls)),
	SND_SOC_DAPM_MIXER("TERT_TDM_RX_5 Audio Mixer", SND_SOC_NOPM, 0, 0,
				tert_tdm_rx_5_mixer_controls,
				ARRAY_SIZE(tert_tdm_rx_5_mixer_controls)),
	SND_SOC_DAPM_MIXER("TERT_TDM_RX_6 Audio Mixer", SND_SOC_NOPM, 0, 0,
				tert_tdm_rx_6_mixer_controls,
				ARRAY_SIZE(tert_tdm_rx_6_mixer_controls)),
	SND_SOC_DAPM_MIXER("TERT_TDM_RX_7 Audio Mixer", SND_SOC_NOPM, 0, 0,
				tert_tdm_rx_7_mixer_controls,
				ARRAY_SIZE(tert_tdm_rx_7_mixer_controls)),

	SND_SOC_DAPM_MIXER("QUAT_TDM_RX_0 Audio Mixer", SND_SOC_NOPM, 0, 0,
				quat_tdm_rx_0_mixer_controls,
				ARRAY_SIZE(quat_tdm_rx_0_mixer_controls)),
	SND_SOC_DAPM_MIXER("QUAT_TDM_RX_1 Audio Mixer", SND_SOC_NOPM, 0, 0,
				quat_tdm_rx_1_mixer_controls,
				ARRAY_SIZE(quat_tdm_rx_1_mixer_controls)),
	SND_SOC_DAPM_MIXER("QUAT_TDM_RX_2 Audio Mixer", SND_SOC_NOPM, 0, 0,
				quat_tdm_rx_2_mixer_controls,
				ARRAY_SIZE(quat_tdm_rx_2_mixer_controls)),
	SND_SOC_DAPM_MIXER("QUAT_TDM_RX_3 Audio Mixer", SND_SOC_NOPM, 0, 0,
				quat_tdm_rx_3_mixer_controls,
				ARRAY_SIZE(quat_tdm_rx_3_mixer_controls)),
	SND_SOC_DAPM_MIXER("QUAT_TDM_RX_4 Audio Mixer", SND_SOC_NOPM, 0, 0,
				quat_tdm_rx_4_mixer_controls,
				ARRAY_SIZE(quat_tdm_rx_4_mixer_controls)),
	SND_SOC_DAPM_MIXER("QUAT_TDM_RX_5 Audio Mixer", SND_SOC_NOPM, 0, 0,
				quat_tdm_rx_5_mixer_controls,
				ARRAY_SIZE(quat_tdm_rx_5_mixer_controls)),
	SND_SOC_DAPM_MIXER("QUAT_TDM_RX_6 Audio Mixer", SND_SOC_NOPM, 0, 0,
				quat_tdm_rx_6_mixer_controls,
				ARRAY_SIZE(quat_tdm_rx_6_mixer_controls)),
	SND_SOC_DAPM_MIXER("QUAT_TDM_RX_7 Audio Mixer", SND_SOC_NOPM, 0, 0,
				quat_tdm_rx_7_mixer_controls,
				ARRAY_SIZE(quat_tdm_rx_7_mixer_controls)),

	SND_SOC_DAPM_MIXER("QUIN_TDM_RX_0 Audio Mixer", SND_SOC_NOPM, 0, 0,
				quin_tdm_rx_0_mixer_controls,
				ARRAY_SIZE(quin_tdm_rx_0_mixer_controls)),
	SND_SOC_DAPM_MIXER("QUIN_TDM_RX_1 Audio Mixer", SND_SOC_NOPM, 0, 0,
				quin_tdm_rx_1_mixer_controls,
				ARRAY_SIZE(quin_tdm_rx_1_mixer_controls)),
	SND_SOC_DAPM_MIXER("QUIN_TDM_RX_2 Audio Mixer", SND_SOC_NOPM, 0, 0,
				quin_tdm_rx_2_mixer_controls,
				ARRAY_SIZE(quin_tdm_rx_2_mixer_controls)),
	SND_SOC_DAPM_MIXER("QUIN_TDM_RX_3 Audio Mixer", SND_SOC_NOPM, 0, 0,
				quin_tdm_rx_3_mixer_controls,
				ARRAY_SIZE(quin_tdm_rx_3_mixer_controls)),
	SND_SOC_DAPM_MIXER("QUIN_TDM_RX_4 Audio Mixer", SND_SOC_NOPM, 0, 0,
				quin_tdm_rx_4_mixer_controls,
				ARRAY_SIZE(quin_tdm_rx_4_mixer_controls)),
	SND_SOC_DAPM_MIXER("QUIN_TDM_RX_5 Audio Mixer", SND_SOC_NOPM, 0, 0,
				quin_tdm_rx_5_mixer_controls,
				ARRAY_SIZE(quin_tdm_rx_5_mixer_controls)),
	SND_SOC_DAPM_MIXER("QUIN_TDM_RX_6 Audio Mixer", SND_SOC_NOPM, 0, 0,
				quin_tdm_rx_6_mixer_controls,
				ARRAY_SIZE(quin_tdm_rx_6_mixer_controls)),
	SND_SOC_DAPM_MIXER("QUIN_TDM_RX_7 Audio Mixer", SND_SOC_NOPM, 0, 0,
				quin_tdm_rx_7_mixer_controls,
				ARRAY_SIZE(quin_tdm_rx_7_mixer_controls)),
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
	Q6ROUTING_RX_DAPM_ROUTE("HDMI Mixer", "HDMI_RX"),
	Q6ROUTING_RX_DAPM_ROUTE("DISPLAY_PORT_RX Audio Mixer",
				"DISPLAY_PORT_RX"),
	Q6ROUTING_RX_DAPM_ROUTE("SLIMBUS_0_RX Audio Mixer", "SLIMBUS_0_RX"),
	Q6ROUTING_RX_DAPM_ROUTE("SLIMBUS_1_RX Audio Mixer", "SLIMBUS_1_RX"),
	Q6ROUTING_RX_DAPM_ROUTE("SLIMBUS_2_RX Audio Mixer", "SLIMBUS_2_RX"),
	Q6ROUTING_RX_DAPM_ROUTE("SLIMBUS_3_RX Audio Mixer", "SLIMBUS_3_RX"),
	Q6ROUTING_RX_DAPM_ROUTE("SLIMBUS_4_RX Audio Mixer", "SLIMBUS_4_RX"),
	Q6ROUTING_RX_DAPM_ROUTE("SLIMBUS_5_RX Audio Mixer", "SLIMBUS_5_RX"),
	Q6ROUTING_RX_DAPM_ROUTE("SLIMBUS_6_RX Audio Mixer", "SLIMBUS_6_RX"),
	Q6ROUTING_RX_DAPM_ROUTE("QUAT_MI2S_RX Audio Mixer", "QUAT_MI2S_RX"),
	Q6ROUTING_RX_DAPM_ROUTE("TERT_MI2S_RX Audio Mixer", "TERT_MI2S_RX"),
	Q6ROUTING_RX_DAPM_ROUTE("SEC_MI2S_RX Audio Mixer", "SEC_MI2S_RX"),
	Q6ROUTING_RX_DAPM_ROUTE("PRI_MI2S_RX Audio Mixer", "PRI_MI2S_RX"),
	Q6ROUTING_RX_DAPM_ROUTE("PRIMARY_TDM_RX_0 Audio Mixer",
				"PRIMARY_TDM_RX_0"),
	Q6ROUTING_RX_DAPM_ROUTE("PRIMARY_TDM_RX_1 Audio Mixer",
				"PRIMARY_TDM_RX_1"),
	Q6ROUTING_RX_DAPM_ROUTE("PRIMARY_TDM_RX_2 Audio Mixer",
				"PRIMARY_TDM_RX_2"),
	Q6ROUTING_RX_DAPM_ROUTE("PRIMARY_TDM_RX_3 Audio Mixer",
				"PRIMARY_TDM_RX_3"),
	Q6ROUTING_RX_DAPM_ROUTE("PRIMARY_TDM_RX_4 Audio Mixer",
				"PRIMARY_TDM_RX_4"),
	Q6ROUTING_RX_DAPM_ROUTE("PRIMARY_TDM_RX_5 Audio Mixer",
				"PRIMARY_TDM_RX_5"),
	Q6ROUTING_RX_DAPM_ROUTE("PRIMARY_TDM_RX_6 Audio Mixer",
				"PRIMARY_TDM_RX_6"),
	Q6ROUTING_RX_DAPM_ROUTE("PRIMARY_TDM_RX_7 Audio Mixer",
				"PRIMARY_TDM_RX_7"),
	Q6ROUTING_RX_DAPM_ROUTE("SEC_TDM_RX_0 Audio Mixer", "SEC_TDM_RX_0"),
	Q6ROUTING_RX_DAPM_ROUTE("SEC_TDM_RX_1 Audio Mixer", "SEC_TDM_RX_1"),
	Q6ROUTING_RX_DAPM_ROUTE("SEC_TDM_RX_2 Audio Mixer", "SEC_TDM_RX_2"),
	Q6ROUTING_RX_DAPM_ROUTE("SEC_TDM_RX_3 Audio Mixer", "SEC_TDM_RX_3"),
	Q6ROUTING_RX_DAPM_ROUTE("SEC_TDM_RX_4 Audio Mixer", "SEC_TDM_RX_4"),
	Q6ROUTING_RX_DAPM_ROUTE("SEC_TDM_RX_5 Audio Mixer", "SEC_TDM_RX_5"),
	Q6ROUTING_RX_DAPM_ROUTE("SEC_TDM_RX_6 Audio Mixer", "SEC_TDM_RX_6"),
	Q6ROUTING_RX_DAPM_ROUTE("SEC_TDM_RX_7 Audio Mixer", "SEC_TDM_RX_7"),
	Q6ROUTING_RX_DAPM_ROUTE("TERT_TDM_RX_0 Audio Mixer", "TERT_TDM_RX_0"),
	Q6ROUTING_RX_DAPM_ROUTE("TERT_TDM_RX_1 Audio Mixer", "TERT_TDM_RX_1"),
	Q6ROUTING_RX_DAPM_ROUTE("TERT_TDM_RX_2 Audio Mixer", "TERT_TDM_RX_2"),
	Q6ROUTING_RX_DAPM_ROUTE("TERT_TDM_RX_3 Audio Mixer", "TERT_TDM_RX_3"),
	Q6ROUTING_RX_DAPM_ROUTE("TERT_TDM_RX_4 Audio Mixer", "TERT_TDM_RX_4"),
	Q6ROUTING_RX_DAPM_ROUTE("TERT_TDM_RX_5 Audio Mixer", "TERT_TDM_RX_5"),
	Q6ROUTING_RX_DAPM_ROUTE("TERT_TDM_RX_6 Audio Mixer", "TERT_TDM_RX_6"),
	Q6ROUTING_RX_DAPM_ROUTE("TERT_TDM_RX_7 Audio Mixer", "TERT_TDM_RX_7"),
	Q6ROUTING_RX_DAPM_ROUTE("QUAT_TDM_RX_0 Audio Mixer", "QUAT_TDM_RX_0"),
	Q6ROUTING_RX_DAPM_ROUTE("QUAT_TDM_RX_1 Audio Mixer", "QUAT_TDM_RX_1"),
	Q6ROUTING_RX_DAPM_ROUTE("QUAT_TDM_RX_2 Audio Mixer", "QUAT_TDM_RX_2"),
	Q6ROUTING_RX_DAPM_ROUTE("QUAT_TDM_RX_3 Audio Mixer", "QUAT_TDM_RX_3"),
	Q6ROUTING_RX_DAPM_ROUTE("QUAT_TDM_RX_4 Audio Mixer", "QUAT_TDM_RX_4"),
	Q6ROUTING_RX_DAPM_ROUTE("QUAT_TDM_RX_5 Audio Mixer", "QUAT_TDM_RX_5"),
	Q6ROUTING_RX_DAPM_ROUTE("QUAT_TDM_RX_6 Audio Mixer", "QUAT_TDM_RX_6"),
	Q6ROUTING_RX_DAPM_ROUTE("QUAT_TDM_RX_7 Audio Mixer", "QUAT_TDM_RX_7"),
	Q6ROUTING_RX_DAPM_ROUTE("QUIN_TDM_RX_0 Audio Mixer", "QUIN_TDM_RX_0"),
	Q6ROUTING_RX_DAPM_ROUTE("QUIN_TDM_RX_1 Audio Mixer", "QUIN_TDM_RX_1"),
	Q6ROUTING_RX_DAPM_ROUTE("QUIN_TDM_RX_2 Audio Mixer", "QUIN_TDM_RX_2"),
	Q6ROUTING_RX_DAPM_ROUTE("QUIN_TDM_RX_3 Audio Mixer", "QUIN_TDM_RX_3"),
	Q6ROUTING_RX_DAPM_ROUTE("QUIN_TDM_RX_4 Audio Mixer", "QUIN_TDM_RX_4"),
	Q6ROUTING_RX_DAPM_ROUTE("QUIN_TDM_RX_5 Audio Mixer", "QUIN_TDM_RX_5"),
	Q6ROUTING_RX_DAPM_ROUTE("QUIN_TDM_RX_6 Audio Mixer", "QUIN_TDM_RX_6"),
	Q6ROUTING_RX_DAPM_ROUTE("QUIN_TDM_RX_7 Audio Mixer", "QUIN_TDM_RX_7"),
	Q6ROUTING_TX_DAPM_ROUTE("MultiMedia1 Mixer"),
	Q6ROUTING_TX_DAPM_ROUTE("MultiMedia2 Mixer"),
	Q6ROUTING_TX_DAPM_ROUTE("MultiMedia3 Mixer"),
	Q6ROUTING_TX_DAPM_ROUTE("MultiMedia4 Mixer"),
	Q6ROUTING_TX_DAPM_ROUTE("MultiMedia5 Mixer"),
	Q6ROUTING_TX_DAPM_ROUTE("MultiMedia6 Mixer"),
	Q6ROUTING_TX_DAPM_ROUTE("MultiMedia7 Mixer"),
	Q6ROUTING_TX_DAPM_ROUTE("MultiMedia8 Mixer"),

	{"MM_UL1", NULL, "MultiMedia1 Mixer"},
	{"MM_UL2", NULL, "MultiMedia2 Mixer"},
	{"MM_UL3", NULL, "MultiMedia3 Mixer"},
	{"MM_UL4", NULL, "MultiMedia4 Mixer"},
	{"MM_UL5", NULL, "MultiMedia5 Mixer"},
	{"MM_UL6", NULL, "MultiMedia6 Mixer"},
	{"MM_UL7", NULL, "MultiMedia7 Mixer"},
	{"MM_UL8", NULL, "MultiMedia8 Mixer"},

	{"MM_DL1",  NULL, "MultiMedia1 Playback" },
	{"MM_DL2",  NULL, "MultiMedia2 Playback" },
	{"MM_DL3",  NULL, "MultiMedia3 Playback" },
	{"MM_DL4",  NULL, "MultiMedia4 Playback" },
	{"MM_DL5",  NULL, "MultiMedia5 Playback" },
	{"MM_DL6",  NULL, "MultiMedia6 Playback" },
	{"MM_DL7",  NULL, "MultiMedia7 Playback" },
	{"MM_DL8",  NULL, "MultiMedia8 Playback" },

	{"MultiMedia1 Capture", NULL, "MM_UL1"},
	{"MultiMedia2 Capture", NULL, "MM_UL2"},
	{"MultiMedia3 Capture", NULL, "MM_UL3"},
	{"MultiMedia4 Capture", NULL, "MM_UL4"},
	{"MultiMedia5 Capture", NULL, "MM_UL5"},
	{"MultiMedia6 Capture", NULL, "MM_UL6"},
	{"MultiMedia7 Capture", NULL, "MM_UL7"},
	{"MultiMedia8 Capture", NULL, "MM_UL8"},

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

	if (be_id >= AFE_MAX_PORTS)
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

	for (i = 0; i < MAX_SESSIONS; i++) {
		routing_data->sessions[i].port_id = -1;
		routing_data->sessions[i].fedai_id = -1;
	}

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

static int q6pcm_routing_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	routing_data = kzalloc(sizeof(*routing_data), GFP_KERNEL);
	if (!routing_data)
		return -ENOMEM;

	routing_data->dev = dev;

	mutex_init(&routing_data->lock);
	dev_set_drvdata(dev, routing_data);

	return devm_snd_soc_register_component(dev, &msm_soc_routing_component,
					  NULL, 0);
}

static int q6pcm_routing_remove(struct platform_device *pdev)
{
	kfree(routing_data);
	routing_data = NULL;

	return 0;
}

static const struct of_device_id q6pcm_routing_device_id[] = {
	{ .compatible = "qcom,q6adm-routing" },
	{},
};
MODULE_DEVICE_TABLE(of, q6pcm_routing_device_id);

static struct platform_driver q6pcm_routing_platform_driver = {
	.driver = {
		.name = "q6routing",
		.of_match_table = of_match_ptr(q6pcm_routing_device_id),
	},
	.probe = q6pcm_routing_probe,
	.remove = q6pcm_routing_remove,
};
module_platform_driver(q6pcm_routing_platform_driver);

MODULE_DESCRIPTION("Q6 Routing platform");
MODULE_LICENSE("GPL v2");
