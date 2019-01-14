/*
 *  hdac_hdmi.c - ASoc HDA-HDMI codec driver for Intel platforms
 *
 *  Copyright (C) 2014-2015 Intel Corp
 *  Author: Samreen Nilofer <samreen.nilofer@intel.com>
 *	    Subhransu S. Prusty <subhransu.s.prusty@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/hdmi.h>
#include <drm/drm_edid.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/hdaudio_ext.h>
#include <sound/hda_i915.h>
#include <sound/pcm_drm_eld.h>
#include <sound/hda_chmap.h>
#include "../../hda/local.h"
#include "hdac_hdmi.h"

#define NAME_SIZE	32

#define AMP_OUT_MUTE		0xb080
#define AMP_OUT_UNMUTE		0xb000
#define PIN_OUT			(AC_PINCTL_OUT_EN)

#define HDA_MAX_CONNECTIONS     32

#define HDA_MAX_CVTS		3
#define HDA_MAX_PORTS		3

#define ELD_MAX_SIZE    256
#define ELD_FIXED_BYTES	20

#define ELD_VER_CEA_861D 2
#define ELD_VER_PARTIAL 31
#define ELD_MAX_MNL     16

struct hdac_hdmi_cvt_params {
	unsigned int channels_min;
	unsigned int channels_max;
	u32 rates;
	u64 formats;
	unsigned int maxbps;
};

struct hdac_hdmi_cvt {
	struct list_head head;
	hda_nid_t nid;
	const char *name;
	struct hdac_hdmi_cvt_params params;
};

/* Currently only spk_alloc, more to be added */
struct hdac_hdmi_parsed_eld {
	u8 spk_alloc;
};

struct hdac_hdmi_eld {
	bool	monitor_present;
	bool	eld_valid;
	int	eld_size;
	char    eld_buffer[ELD_MAX_SIZE];
	struct	hdac_hdmi_parsed_eld info;
};

struct hdac_hdmi_pin {
	struct list_head head;
	hda_nid_t nid;
	bool mst_capable;
	struct hdac_hdmi_port *ports;
	int num_ports;
	struct hdac_device *hdev;
};

struct hdac_hdmi_port {
	struct list_head head;
	int id;
	struct hdac_hdmi_pin *pin;
	int num_mux_nids;
	hda_nid_t mux_nids[HDA_MAX_CONNECTIONS];
	struct hdac_hdmi_eld eld;
	const char *jack_pin;
	struct snd_soc_dapm_context *dapm;
	const char *output_pin;
};

struct hdac_hdmi_pcm {
	struct list_head head;
	int pcm_id;
	struct list_head port_list;
	struct hdac_hdmi_cvt *cvt;
	struct snd_soc_jack *jack;
	int stream_tag;
	int channels;
	int format;
	bool chmap_set;
	unsigned char chmap[8]; /* ALSA API channel-map */
	struct mutex lock;
	int jack_event;
};

struct hdac_hdmi_dai_port_map {
	int dai_id;
	struct hdac_hdmi_port *port;
	struct hdac_hdmi_cvt *cvt;
};

struct hdac_hdmi_drv_data {
	unsigned int vendor_nid;
};

struct hdac_hdmi_priv {
	struct hdac_device *hdev;
	struct snd_soc_component *component;
	struct snd_card *card;
	struct hdac_hdmi_dai_port_map dai_map[HDA_MAX_CVTS];
	struct list_head pin_list;
	struct list_head cvt_list;
	struct list_head pcm_list;
	int num_pin;
	int num_cvt;
	int num_ports;
	struct mutex pin_mutex;
	struct hdac_chmap chmap;
	struct hdac_hdmi_drv_data *drv_data;
	struct snd_soc_dai_driver *dai_drv;
};

#define hdev_to_hdmi_priv(_hdev) dev_get_drvdata(&(_hdev)->dev)

static struct hdac_hdmi_pcm *
hdac_hdmi_get_pcm_from_cvt(struct hdac_hdmi_priv *hdmi,
			   struct hdac_hdmi_cvt *cvt)
{
	struct hdac_hdmi_pcm *pcm = NULL;

	list_for_each_entry(pcm, &hdmi->pcm_list, head) {
		if (pcm->cvt == cvt)
			break;
	}

	return pcm;
}

static void hdac_hdmi_jack_report(struct hdac_hdmi_pcm *pcm,
		struct hdac_hdmi_port *port, bool is_connect)
{
	struct hdac_device *hdev = port->pin->hdev;

	if (is_connect)
		snd_soc_dapm_enable_pin(port->dapm, port->jack_pin);
	else
		snd_soc_dapm_disable_pin(port->dapm, port->jack_pin);

	if (is_connect) {
		/*
		 * Report Jack connect event when a device is connected
		 * for the first time where same PCM is attached to multiple
		 * ports.
		 */
		if (pcm->jack_event == 0) {
			dev_dbg(&hdev->dev,
					"jack report for pcm=%d\n",
					pcm->pcm_id);
			snd_soc_jack_report(pcm->jack, SND_JACK_AVOUT,
						SND_JACK_AVOUT);
		}
		pcm->jack_event++;
	} else {
		/*
		 * Report Jack disconnect event when a device is disconnected
		 * is the only last connected device when same PCM is attached
		 * to multiple ports.
		 */
		if (pcm->jack_event == 1)
			snd_soc_jack_report(pcm->jack, 0, SND_JACK_AVOUT);
		if (pcm->jack_event > 0)
			pcm->jack_event--;
	}

	snd_soc_dapm_sync(port->dapm);
}

/* MST supported verbs */
/*
 * Get the no devices that can be connected to a port on the Pin widget.
 */
static int hdac_hdmi_get_port_len(struct hdac_device *hdev, hda_nid_t nid)
{
	unsigned int caps;
	unsigned int type, param;

	caps = get_wcaps(hdev, nid);
	type = get_wcaps_type(caps);

	if (!(caps & AC_WCAP_DIGITAL) || (type != AC_WID_PIN))
		return 0;

	param = snd_hdac_read_parm_uncached(hdev, nid, AC_PAR_DEVLIST_LEN);
	if (param == -1)
		return param;

	return param & AC_DEV_LIST_LEN_MASK;
}

/*
 * Get the port entry select on the pin. Return the port entry
 * id selected on the pin. Return 0 means the first port entry
 * is selected or MST is not supported.
 */
static int hdac_hdmi_port_select_get(struct hdac_device *hdev,
					struct hdac_hdmi_port *port)
{
	return snd_hdac_codec_read(hdev, port->pin->nid,
				0, AC_VERB_GET_DEVICE_SEL, 0);
}

/*
 * Sets the selected port entry for the configuring Pin widget verb.
 * returns error if port set is not equal to port get otherwise success
 */
static int hdac_hdmi_port_select_set(struct hdac_device *hdev,
					struct hdac_hdmi_port *port)
{
	int num_ports;

	if (!port->pin->mst_capable)
		return 0;

	/* AC_PAR_DEVLIST_LEN is 0 based. */
	num_ports = hdac_hdmi_get_port_len(hdev, port->pin->nid);
	if (num_ports < 0)
		return -EIO;
	/*
	 * Device List Length is a 0 based integer value indicating the
	 * number of sink device that a MST Pin Widget can support.
	 */
	if (num_ports + 1  < port->id)
		return 0;

	snd_hdac_codec_write(hdev, port->pin->nid, 0,
			AC_VERB_SET_DEVICE_SEL, port->id);

	if (port->id != hdac_hdmi_port_select_get(hdev, port))
		return -EIO;

	dev_dbg(&hdev->dev, "Selected the port=%d\n", port->id);

	return 0;
}

static struct hdac_hdmi_pcm *get_hdmi_pcm_from_id(struct hdac_hdmi_priv *hdmi,
						int pcm_idx)
{
	struct hdac_hdmi_pcm *pcm;

	list_for_each_entry(pcm, &hdmi->pcm_list, head) {
		if (pcm->pcm_id == pcm_idx)
			return pcm;
	}

	return NULL;
}

static unsigned int sad_format(const u8 *sad)
{
	return ((sad[0] >> 0x3) & 0x1f);
}

static unsigned int sad_sample_bits_lpcm(const u8 *sad)
{
	return (sad[2] & 7);
}

static int hdac_hdmi_eld_limit_formats(struct snd_pcm_runtime *runtime,
						void *eld)
{
	u64 formats = SNDRV_PCM_FMTBIT_S16;
	int i;
	const u8 *sad, *eld_buf = eld;

	sad = drm_eld_sad(eld_buf);
	if (!sad)
		goto format_constraint;

	for (i = drm_eld_sad_count(eld_buf); i > 0; i--, sad += 3) {
		if (sad_format(sad) == 1) { /* AUDIO_CODING_TYPE_LPCM */

			/*
			 * the controller support 20 and 24 bits in 32 bit
			 * container so we set S32
			 */
			if (sad_sample_bits_lpcm(sad) & 0x6)
				formats |= SNDRV_PCM_FMTBIT_S32;
		}
	}

format_constraint:
	return snd_pcm_hw_constraint_mask64(runtime, SNDRV_PCM_HW_PARAM_FORMAT,
				formats);

}

static void
hdac_hdmi_set_dip_index(struct hdac_device *hdev, hda_nid_t pin_nid,
				int packet_index, int byte_index)
{
	int val;

	val = (packet_index << 5) | (byte_index & 0x1f);
	snd_hdac_codec_write(hdev, pin_nid, 0, AC_VERB_SET_HDMI_DIP_INDEX, val);
}

struct dp_audio_infoframe {
	u8 type; /* 0x84 */
	u8 len;  /* 0x1b */
	u8 ver;  /* 0x11 << 2 */

	u8 CC02_CT47;	/* match with HDMI infoframe from this on */
	u8 SS01_SF24;
	u8 CXT04;
	u8 CA;
	u8 LFEPBL01_LSV36_DM_INH7;
};

static int hdac_hdmi_setup_audio_infoframe(struct hdac_device *hdev,
		   struct hdac_hdmi_pcm *pcm, struct hdac_hdmi_port *port)
{
	uint8_t buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_AUDIO_INFOFRAME_SIZE];
	struct hdmi_audio_infoframe frame;
	struct hdac_hdmi_pin *pin = port->pin;
	struct dp_audio_infoframe dp_ai;
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	struct hdac_hdmi_cvt *cvt = pcm->cvt;
	u8 *dip;
	int ret;
	int i;
	const u8 *eld_buf;
	u8 conn_type;
	int channels, ca;

	ca = snd_hdac_channel_allocation(hdev, port->eld.info.spk_alloc,
			pcm->channels, pcm->chmap_set, true, pcm->chmap);

	channels = snd_hdac_get_active_channels(ca);
	hdmi->chmap.ops.set_channel_count(hdev, cvt->nid, channels);

	snd_hdac_setup_channel_mapping(&hdmi->chmap, pin->nid, false, ca,
				pcm->channels, pcm->chmap, pcm->chmap_set);

	eld_buf = port->eld.eld_buffer;
	conn_type = drm_eld_get_conn_type(eld_buf);

	switch (conn_type) {
	case DRM_ELD_CONN_TYPE_HDMI:
		hdmi_audio_infoframe_init(&frame);

		frame.channels = channels;
		frame.channel_allocation = ca;

		ret = hdmi_audio_infoframe_pack(&frame, buffer, sizeof(buffer));
		if (ret < 0)
			return ret;

		break;

	case DRM_ELD_CONN_TYPE_DP:
		memset(&dp_ai, 0, sizeof(dp_ai));
		dp_ai.type	= 0x84;
		dp_ai.len	= 0x1b;
		dp_ai.ver	= 0x11 << 2;
		dp_ai.CC02_CT47	= channels - 1;
		dp_ai.CA	= ca;

		dip = (u8 *)&dp_ai;
		break;

	default:
		dev_err(&hdev->dev, "Invalid connection type: %d\n", conn_type);
		return -EIO;
	}

	/* stop infoframe transmission */
	hdac_hdmi_set_dip_index(hdev, pin->nid, 0x0, 0x0);
	snd_hdac_codec_write(hdev, pin->nid, 0,
			AC_VERB_SET_HDMI_DIP_XMIT, AC_DIPXMIT_DISABLE);


	/*  Fill infoframe. Index auto-incremented */
	hdac_hdmi_set_dip_index(hdev, pin->nid, 0x0, 0x0);
	if (conn_type == DRM_ELD_CONN_TYPE_HDMI) {
		for (i = 0; i < sizeof(buffer); i++)
			snd_hdac_codec_write(hdev, pin->nid, 0,
				AC_VERB_SET_HDMI_DIP_DATA, buffer[i]);
	} else {
		for (i = 0; i < sizeof(dp_ai); i++)
			snd_hdac_codec_write(hdev, pin->nid, 0,
				AC_VERB_SET_HDMI_DIP_DATA, dip[i]);
	}

	/* Start infoframe */
	hdac_hdmi_set_dip_index(hdev, pin->nid, 0x0, 0x0);
	snd_hdac_codec_write(hdev, pin->nid, 0,
			AC_VERB_SET_HDMI_DIP_XMIT, AC_DIPXMIT_BEST);

	return 0;
}

static int hdac_hdmi_set_tdm_slot(struct snd_soc_dai *dai,
		unsigned int tx_mask, unsigned int rx_mask,
		int slots, int slot_width)
{
	struct hdac_hdmi_priv *hdmi = snd_soc_dai_get_drvdata(dai);
	struct hdac_device *hdev = hdmi->hdev;
	struct hdac_hdmi_dai_port_map *dai_map;
	struct hdac_hdmi_pcm *pcm;

	dev_dbg(&hdev->dev, "%s: strm_tag: %d\n", __func__, tx_mask);

	dai_map = &hdmi->dai_map[dai->id];

	pcm = hdac_hdmi_get_pcm_from_cvt(hdmi, dai_map->cvt);

	if (pcm)
		pcm->stream_tag = (tx_mask << 4);

	return 0;
}

static int hdac_hdmi_set_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *hparams, struct snd_soc_dai *dai)
{
	struct hdac_hdmi_priv *hdmi = snd_soc_dai_get_drvdata(dai);
	struct hdac_device *hdev = hdmi->hdev;
	struct hdac_hdmi_dai_port_map *dai_map;
	struct hdac_hdmi_port *port;
	struct hdac_hdmi_pcm *pcm;
	int format;

	dai_map = &hdmi->dai_map[dai->id];
	port = dai_map->port;

	if (!port)
		return -ENODEV;

	if ((!port->eld.monitor_present) || (!port->eld.eld_valid)) {
		dev_err(&hdev->dev,
			"device is not configured for this pin:port%d:%d\n",
					port->pin->nid, port->id);
		return -ENODEV;
	}

	format = snd_hdac_calc_stream_format(params_rate(hparams),
			params_channels(hparams), params_format(hparams),
			dai->driver->playback.sig_bits, 0);

	pcm = hdac_hdmi_get_pcm_from_cvt(hdmi, dai_map->cvt);
	if (!pcm)
		return -EIO;

	pcm->format = format;
	pcm->channels = params_channels(hparams);

	return 0;
}

static int hdac_hdmi_query_port_connlist(struct hdac_device *hdev,
					struct hdac_hdmi_pin *pin,
					struct hdac_hdmi_port *port)
{
	if (!(get_wcaps(hdev, pin->nid) & AC_WCAP_CONN_LIST)) {
		dev_warn(&hdev->dev,
			"HDMI: pin %d wcaps %#x does not support connection list\n",
			pin->nid, get_wcaps(hdev, pin->nid));
		return -EINVAL;
	}

	if (hdac_hdmi_port_select_set(hdev, port) < 0)
		return -EIO;

	port->num_mux_nids = snd_hdac_get_connections(hdev, pin->nid,
			port->mux_nids, HDA_MAX_CONNECTIONS);
	if (port->num_mux_nids == 0)
		dev_warn(&hdev->dev,
			"No connections found for pin:port %d:%d\n",
						pin->nid, port->id);

	dev_dbg(&hdev->dev, "num_mux_nids %d for pin:port %d:%d\n",
			port->num_mux_nids, pin->nid, port->id);

	return port->num_mux_nids;
}

/*
 * Query pcm list and return port to which stream is routed.
 *
 * Also query connection list of the pin, to validate the cvt to port map.
 *
 * Same stream rendering to multiple ports simultaneously can be done
 * possibly, but not supported for now in driver. So return the first port
 * connected.
 */
static struct hdac_hdmi_port *hdac_hdmi_get_port_from_cvt(
			struct hdac_device *hdev,
			struct hdac_hdmi_priv *hdmi,
			struct hdac_hdmi_cvt *cvt)
{
	struct hdac_hdmi_pcm *pcm;
	struct hdac_hdmi_port *port = NULL;
	int ret, i;

	list_for_each_entry(pcm, &hdmi->pcm_list, head) {
		if (pcm->cvt == cvt) {
			if (list_empty(&pcm->port_list))
				continue;

			list_for_each_entry(port, &pcm->port_list, head) {
				mutex_lock(&pcm->lock);
				ret = hdac_hdmi_query_port_connlist(hdev,
							port->pin, port);
				mutex_unlock(&pcm->lock);
				if (ret < 0)
					continue;

				for (i = 0; i < port->num_mux_nids; i++) {
					if (port->mux_nids[i] == cvt->nid &&
						port->eld.monitor_present &&
						port->eld.eld_valid)
						return port;
				}
			}
		}
	}

	return NULL;
}

/*
 * This tries to get a valid pin and set the HW constraints based on the
 * ELD. Even if a valid pin is not found return success so that device open
 * doesn't fail.
 */
static int hdac_hdmi_pcm_open(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct hdac_hdmi_priv *hdmi = snd_soc_dai_get_drvdata(dai);
	struct hdac_device *hdev = hdmi->hdev;
	struct hdac_hdmi_dai_port_map *dai_map;
	struct hdac_hdmi_cvt *cvt;
	struct hdac_hdmi_port *port;
	int ret;

	dai_map = &hdmi->dai_map[dai->id];

	cvt = dai_map->cvt;
	port = hdac_hdmi_get_port_from_cvt(hdev, hdmi, cvt);

	/*
	 * To make PA and other userland happy.
	 * userland scans devices so returning error does not help.
	 */
	if (!port)
		return 0;
	if ((!port->eld.monitor_present) ||
			(!port->eld.eld_valid)) {

		dev_warn(&hdev->dev,
			"Failed: present?:%d ELD valid?:%d pin:port: %d:%d\n",
			port->eld.monitor_present, port->eld.eld_valid,
			port->pin->nid, port->id);

		return 0;
	}

	dai_map->port = port;

	ret = hdac_hdmi_eld_limit_formats(substream->runtime,
				port->eld.eld_buffer);
	if (ret < 0)
		return ret;

	return snd_pcm_hw_constraint_eld(substream->runtime,
				port->eld.eld_buffer);
}

static void hdac_hdmi_pcm_close(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct hdac_hdmi_priv *hdmi = snd_soc_dai_get_drvdata(dai);
	struct hdac_hdmi_dai_port_map *dai_map;
	struct hdac_hdmi_pcm *pcm;

	dai_map = &hdmi->dai_map[dai->id];

	pcm = hdac_hdmi_get_pcm_from_cvt(hdmi, dai_map->cvt);

	if (pcm) {
		mutex_lock(&pcm->lock);
		pcm->chmap_set = false;
		memset(pcm->chmap, 0, sizeof(pcm->chmap));
		pcm->channels = 0;
		mutex_unlock(&pcm->lock);
	}

	if (dai_map->port)
		dai_map->port = NULL;
}

static int
hdac_hdmi_query_cvt_params(struct hdac_device *hdev, struct hdac_hdmi_cvt *cvt)
{
	unsigned int chans;
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	int err;

	chans = get_wcaps(hdev, cvt->nid);
	chans = get_wcaps_channels(chans);

	cvt->params.channels_min = 2;

	cvt->params.channels_max = chans;
	if (chans > hdmi->chmap.channels_max)
		hdmi->chmap.channels_max = chans;

	err = snd_hdac_query_supported_pcm(hdev, cvt->nid,
			&cvt->params.rates,
			&cvt->params.formats,
			&cvt->params.maxbps);
	if (err < 0)
		dev_err(&hdev->dev,
			"Failed to query pcm params for nid %d: %d\n",
			cvt->nid, err);

	return err;
}

static int hdac_hdmi_fill_widget_info(struct device *dev,
		struct snd_soc_dapm_widget *w, enum snd_soc_dapm_type id,
		void *priv, const char *wname, const char *stream,
		struct snd_kcontrol_new *wc, int numkc,
		int (*event)(struct snd_soc_dapm_widget *,
		struct snd_kcontrol *, int), unsigned short event_flags)
{
	w->id = id;
	w->name = devm_kstrdup(dev, wname, GFP_KERNEL);
	if (!w->name)
		return -ENOMEM;

	w->sname = stream;
	w->reg = SND_SOC_NOPM;
	w->shift = 0;
	w->kcontrol_news = wc;
	w->num_kcontrols = numkc;
	w->priv = priv;
	w->event = event;
	w->event_flags = event_flags;

	return 0;
}

static void hdac_hdmi_fill_route(struct snd_soc_dapm_route *route,
		const char *sink, const char *control, const char *src,
		int (*handler)(struct snd_soc_dapm_widget *src,
			struct snd_soc_dapm_widget *sink))
{
	route->sink = sink;
	route->source = src;
	route->control = control;
	route->connected = handler;
}

static struct hdac_hdmi_pcm *hdac_hdmi_get_pcm(struct hdac_device *hdev,
					struct hdac_hdmi_port *port)
{
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	struct hdac_hdmi_pcm *pcm = NULL;
	struct hdac_hdmi_port *p;

	list_for_each_entry(pcm, &hdmi->pcm_list, head) {
		if (list_empty(&pcm->port_list))
			continue;

		list_for_each_entry(p, &pcm->port_list, head) {
			if (p->id == port->id && port->pin == p->pin)
				return pcm;
		}
	}

	return NULL;
}

static void hdac_hdmi_set_power_state(struct hdac_device *hdev,
			     hda_nid_t nid, unsigned int pwr_state)
{
	int count;
	unsigned int state;

	if (get_wcaps(hdev, nid) & AC_WCAP_POWER) {
		if (!snd_hdac_check_power_state(hdev, nid, pwr_state)) {
			for (count = 0; count < 10; count++) {
				snd_hdac_codec_read(hdev, nid, 0,
						AC_VERB_SET_POWER_STATE,
						pwr_state);
				state = snd_hdac_sync_power_state(hdev,
						nid, pwr_state);
				if (!(state & AC_PWRST_ERROR))
					break;
			}
		}
	}
}

static void hdac_hdmi_set_amp(struct hdac_device *hdev,
				   hda_nid_t nid, int val)
{
	if (get_wcaps(hdev, nid) & AC_WCAP_OUT_AMP)
		snd_hdac_codec_write(hdev, nid, 0,
					AC_VERB_SET_AMP_GAIN_MUTE, val);
}


static int hdac_hdmi_pin_output_widget_event(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kc, int event)
{
	struct hdac_hdmi_port *port = w->priv;
	struct hdac_device *hdev = dev_to_hdac_dev(w->dapm->dev);
	struct hdac_hdmi_pcm *pcm;

	dev_dbg(&hdev->dev, "%s: widget: %s event: %x\n",
			__func__, w->name, event);

	pcm = hdac_hdmi_get_pcm(hdev, port);
	if (!pcm)
		return -EIO;

	/* set the device if pin is mst_capable */
	if (hdac_hdmi_port_select_set(hdev, port) < 0)
		return -EIO;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		hdac_hdmi_set_power_state(hdev, port->pin->nid, AC_PWRST_D0);

		/* Enable out path for this pin widget */
		snd_hdac_codec_write(hdev, port->pin->nid, 0,
				AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);

		hdac_hdmi_set_amp(hdev, port->pin->nid, AMP_OUT_UNMUTE);

		return hdac_hdmi_setup_audio_infoframe(hdev, pcm, port);

	case SND_SOC_DAPM_POST_PMD:
		hdac_hdmi_set_amp(hdev, port->pin->nid, AMP_OUT_MUTE);

		/* Disable out path for this pin widget */
		snd_hdac_codec_write(hdev, port->pin->nid, 0,
				AC_VERB_SET_PIN_WIDGET_CONTROL, 0);

		hdac_hdmi_set_power_state(hdev, port->pin->nid, AC_PWRST_D3);
		break;

	}

	return 0;
}

static int hdac_hdmi_cvt_output_widget_event(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kc, int event)
{
	struct hdac_hdmi_cvt *cvt = w->priv;
	struct hdac_device *hdev = dev_to_hdac_dev(w->dapm->dev);
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	struct hdac_hdmi_pcm *pcm;

	dev_dbg(&hdev->dev, "%s: widget: %s event: %x\n",
			__func__, w->name, event);

	pcm = hdac_hdmi_get_pcm_from_cvt(hdmi, cvt);
	if (!pcm)
		return -EIO;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		hdac_hdmi_set_power_state(hdev, cvt->nid, AC_PWRST_D0);

		/* Enable transmission */
		snd_hdac_codec_write(hdev, cvt->nid, 0,
			AC_VERB_SET_DIGI_CONVERT_1, 1);

		/* Category Code (CC) to zero */
		snd_hdac_codec_write(hdev, cvt->nid, 0,
			AC_VERB_SET_DIGI_CONVERT_2, 0);

		snd_hdac_codec_write(hdev, cvt->nid, 0,
				AC_VERB_SET_CHANNEL_STREAMID, pcm->stream_tag);
		snd_hdac_codec_write(hdev, cvt->nid, 0,
				AC_VERB_SET_STREAM_FORMAT, pcm->format);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_hdac_codec_write(hdev, cvt->nid, 0,
				AC_VERB_SET_CHANNEL_STREAMID, 0);
		snd_hdac_codec_write(hdev, cvt->nid, 0,
				AC_VERB_SET_STREAM_FORMAT, 0);

		hdac_hdmi_set_power_state(hdev, cvt->nid, AC_PWRST_D3);
		break;

	}

	return 0;
}

static int hdac_hdmi_pin_mux_widget_event(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kc, int event)
{
	struct hdac_hdmi_port *port = w->priv;
	struct hdac_device *hdev = dev_to_hdac_dev(w->dapm->dev);
	int mux_idx;

	dev_dbg(&hdev->dev, "%s: widget: %s event: %x\n",
			__func__, w->name, event);

	if (!kc)
		kc  = w->kcontrols[0];

	mux_idx = dapm_kcontrol_get_value(kc);

	/* set the device if pin is mst_capable */
	if (hdac_hdmi_port_select_set(hdev, port) < 0)
		return -EIO;

	if (mux_idx > 0) {
		snd_hdac_codec_write(hdev, port->pin->nid, 0,
			AC_VERB_SET_CONNECT_SEL, (mux_idx - 1));
	}

	return 0;
}

/*
 * Based on user selection, map the PINs with the PCMs.
 */
static int hdac_hdmi_set_pin_port_mux(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ret;
	struct hdac_hdmi_port *p, *p_next;
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_dapm_widget *w = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct hdac_hdmi_port *port = w->priv;
	struct hdac_device *hdev = dev_to_hdac_dev(dapm->dev);
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	struct hdac_hdmi_pcm *pcm = NULL;
	const char *cvt_name =  e->texts[ucontrol->value.enumerated.item[0]];

	ret = snd_soc_dapm_put_enum_double(kcontrol, ucontrol);
	if (ret < 0)
		return ret;

	if (port == NULL)
		return -EINVAL;

	mutex_lock(&hdmi->pin_mutex);
	list_for_each_entry(pcm, &hdmi->pcm_list, head) {
		if (list_empty(&pcm->port_list))
			continue;

		list_for_each_entry_safe(p, p_next, &pcm->port_list, head) {
			if (p == port && p->id == port->id &&
					p->pin == port->pin) {
				hdac_hdmi_jack_report(pcm, port, false);
				list_del(&p->head);
			}
		}
	}

	/*
	 * Jack status is not reported during device probe as the
	 * PCMs are not registered by then. So report it here.
	 */
	list_for_each_entry(pcm, &hdmi->pcm_list, head) {
		if (!strcmp(cvt_name, pcm->cvt->name)) {
			list_add_tail(&port->head, &pcm->port_list);
			if (port->eld.monitor_present && port->eld.eld_valid) {
				hdac_hdmi_jack_report(pcm, port, true);
				mutex_unlock(&hdmi->pin_mutex);
				return ret;
			}
		}
	}
	mutex_unlock(&hdmi->pin_mutex);

	return ret;
}

/*
 * Ideally the Mux inputs should be based on the num_muxs enumerated, but
 * the display driver seem to be programming the connection list for the pin
 * widget runtime.
 *
 * So programming all the possible inputs for the mux, the user has to take
 * care of selecting the right one and leaving all other inputs selected to
 * "NONE"
 */
static int hdac_hdmi_create_pin_port_muxs(struct hdac_device *hdev,
				struct hdac_hdmi_port *port,
				struct snd_soc_dapm_widget *widget,
				const char *widget_name)
{
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	struct hdac_hdmi_pin *pin = port->pin;
	struct snd_kcontrol_new *kc;
	struct hdac_hdmi_cvt *cvt;
	struct soc_enum *se;
	char kc_name[NAME_SIZE];
	char mux_items[NAME_SIZE];
	/* To hold inputs to the Pin mux */
	char *items[HDA_MAX_CONNECTIONS];
	int i = 0;
	int num_items = hdmi->num_cvt + 1;

	kc = devm_kzalloc(&hdev->dev, sizeof(*kc), GFP_KERNEL);
	if (!kc)
		return -ENOMEM;

	se = devm_kzalloc(&hdev->dev, sizeof(*se), GFP_KERNEL);
	if (!se)
		return -ENOMEM;

	snprintf(kc_name, NAME_SIZE, "Pin %d port %d Input",
						pin->nid, port->id);
	kc->name = devm_kstrdup(&hdev->dev, kc_name, GFP_KERNEL);
	if (!kc->name)
		return -ENOMEM;

	kc->private_value = (long)se;
	kc->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	kc->access = 0;
	kc->info = snd_soc_info_enum_double;
	kc->put = hdac_hdmi_set_pin_port_mux;
	kc->get = snd_soc_dapm_get_enum_double;

	se->reg = SND_SOC_NOPM;

	/* enum texts: ["NONE", "cvt #", "cvt #", ...] */
	se->items = num_items;
	se->mask = roundup_pow_of_two(se->items) - 1;

	sprintf(mux_items, "NONE");
	items[i] = devm_kstrdup(&hdev->dev, mux_items, GFP_KERNEL);
	if (!items[i])
		return -ENOMEM;

	list_for_each_entry(cvt, &hdmi->cvt_list, head) {
		i++;
		sprintf(mux_items, "cvt %d", cvt->nid);
		items[i] = devm_kstrdup(&hdev->dev, mux_items, GFP_KERNEL);
		if (!items[i])
			return -ENOMEM;
	}

	se->texts = devm_kmemdup(&hdev->dev, items,
			(num_items  * sizeof(char *)), GFP_KERNEL);
	if (!se->texts)
		return -ENOMEM;

	return hdac_hdmi_fill_widget_info(&hdev->dev, widget,
			snd_soc_dapm_mux, port, widget_name, NULL, kc, 1,
			hdac_hdmi_pin_mux_widget_event,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_REG);
}

/* Add cvt <- input <- mux route map */
static void hdac_hdmi_add_pinmux_cvt_route(struct hdac_device *hdev,
			struct snd_soc_dapm_widget *widgets,
			struct snd_soc_dapm_route *route, int rindex)
{
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	const struct snd_kcontrol_new *kc;
	struct soc_enum *se;
	int mux_index = hdmi->num_cvt + hdmi->num_ports;
	int i, j;

	for (i = 0; i < hdmi->num_ports; i++) {
		kc = widgets[mux_index].kcontrol_news;
		se = (struct soc_enum *)kc->private_value;
		for (j = 0; j < hdmi->num_cvt; j++) {
			hdac_hdmi_fill_route(&route[rindex],
					widgets[mux_index].name,
					se->texts[j + 1],
					widgets[j].name, NULL);

			rindex++;
		}

		mux_index++;
	}
}

/*
 * Widgets are added in the below sequence
 *	Converter widgets for num converters enumerated
 *	Pin-port widgets for num ports for Pins enumerated
 *	Pin-port mux widgets to represent connenction list of pin widget
 *
 * For each port, one Mux and One output widget is added
 * Total widgets elements = num_cvt + (num_ports * 2);
 *
 * Routes are added as below:
 *	pin-port mux -> pin (based on num_ports)
 *	cvt -> "Input sel control" -> pin-port_mux
 *
 * Total route elements:
 *	num_ports + (pin_muxes * num_cvt)
 */
static int create_fill_widget_route_map(struct snd_soc_dapm_context *dapm)
{
	struct snd_soc_dapm_widget *widgets;
	struct snd_soc_dapm_route *route;
	struct hdac_device *hdev = dev_to_hdac_dev(dapm->dev);
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	struct snd_soc_dai_driver *dai_drv = hdmi->dai_drv;
	char widget_name[NAME_SIZE];
	struct hdac_hdmi_cvt *cvt;
	struct hdac_hdmi_pin *pin;
	int ret, i = 0, num_routes = 0, j;

	if (list_empty(&hdmi->cvt_list) || list_empty(&hdmi->pin_list))
		return -EINVAL;

	widgets = devm_kzalloc(dapm->dev, (sizeof(*widgets) *
				((2 * hdmi->num_ports) + hdmi->num_cvt)),
				GFP_KERNEL);

	if (!widgets)
		return -ENOMEM;

	/* DAPM widgets to represent each converter widget */
	list_for_each_entry(cvt, &hdmi->cvt_list, head) {
		sprintf(widget_name, "Converter %d", cvt->nid);
		ret = hdac_hdmi_fill_widget_info(dapm->dev, &widgets[i],
			snd_soc_dapm_aif_in, cvt,
			widget_name, dai_drv[i].playback.stream_name, NULL, 0,
			hdac_hdmi_cvt_output_widget_event,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD);
		if (ret < 0)
			return ret;
		i++;
	}

	list_for_each_entry(pin, &hdmi->pin_list, head) {
		for (j = 0; j < pin->num_ports; j++) {
			sprintf(widget_name, "hif%d-%d Output",
				pin->nid, pin->ports[j].id);
			ret = hdac_hdmi_fill_widget_info(dapm->dev, &widgets[i],
					snd_soc_dapm_output, &pin->ports[j],
					widget_name, NULL, NULL, 0,
					hdac_hdmi_pin_output_widget_event,
					SND_SOC_DAPM_PRE_PMU |
					SND_SOC_DAPM_POST_PMD);
			if (ret < 0)
				return ret;
			pin->ports[j].output_pin = widgets[i].name;
			i++;
		}
	}

	/* DAPM widgets to represent the connection list to pin widget */
	list_for_each_entry(pin, &hdmi->pin_list, head) {
		for (j = 0; j < pin->num_ports; j++) {
			sprintf(widget_name, "Pin%d-Port%d Mux",
				pin->nid, pin->ports[j].id);
			ret = hdac_hdmi_create_pin_port_muxs(hdev,
						&pin->ports[j], &widgets[i],
						widget_name);
			if (ret < 0)
				return ret;
			i++;

			/* For cvt to pin_mux mapping */
			num_routes += hdmi->num_cvt;

			/* For pin_mux to pin mapping */
			num_routes++;
		}
	}

	route = devm_kzalloc(dapm->dev, (sizeof(*route) * num_routes),
							GFP_KERNEL);
	if (!route)
		return -ENOMEM;

	i = 0;
	/* Add pin <- NULL <- mux route map */
	list_for_each_entry(pin, &hdmi->pin_list, head) {
		for (j = 0; j < pin->num_ports; j++) {
			int sink_index = i + hdmi->num_cvt;
			int src_index = sink_index + pin->num_ports *
						hdmi->num_pin;

			hdac_hdmi_fill_route(&route[i],
				widgets[sink_index].name, NULL,
				widgets[src_index].name, NULL);
			i++;
		}
	}

	hdac_hdmi_add_pinmux_cvt_route(hdev, widgets, route, i);

	snd_soc_dapm_new_controls(dapm, widgets,
		((2 * hdmi->num_ports) + hdmi->num_cvt));

	snd_soc_dapm_add_routes(dapm, route, num_routes);
	snd_soc_dapm_new_widgets(dapm->card);

	return 0;

}

static int hdac_hdmi_init_dai_map(struct hdac_device *hdev)
{
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	struct hdac_hdmi_dai_port_map *dai_map;
	struct hdac_hdmi_cvt *cvt;
	int dai_id = 0;

	if (list_empty(&hdmi->cvt_list))
		return -EINVAL;

	list_for_each_entry(cvt, &hdmi->cvt_list, head) {
		dai_map = &hdmi->dai_map[dai_id];
		dai_map->dai_id = dai_id;
		dai_map->cvt = cvt;

		dai_id++;

		if (dai_id == HDA_MAX_CVTS) {
			dev_warn(&hdev->dev,
				"Max dais supported: %d\n", dai_id);
			break;
		}
	}

	return 0;
}

static int hdac_hdmi_add_cvt(struct hdac_device *hdev, hda_nid_t nid)
{
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	struct hdac_hdmi_cvt *cvt;
	char name[NAME_SIZE];

	cvt = kzalloc(sizeof(*cvt), GFP_KERNEL);
	if (!cvt)
		return -ENOMEM;

	cvt->nid = nid;
	sprintf(name, "cvt %d", cvt->nid);
	cvt->name = kstrdup(name, GFP_KERNEL);

	list_add_tail(&cvt->head, &hdmi->cvt_list);
	hdmi->num_cvt++;

	return hdac_hdmi_query_cvt_params(hdev, cvt);
}

static int hdac_hdmi_parse_eld(struct hdac_device *hdev,
			struct hdac_hdmi_port *port)
{
	unsigned int ver, mnl;

	ver = (port->eld.eld_buffer[DRM_ELD_VER] & DRM_ELD_VER_MASK)
						>> DRM_ELD_VER_SHIFT;

	if (ver != ELD_VER_CEA_861D && ver != ELD_VER_PARTIAL) {
		dev_err(&hdev->dev, "HDMI: Unknown ELD version %d\n", ver);
		return -EINVAL;
	}

	mnl = (port->eld.eld_buffer[DRM_ELD_CEA_EDID_VER_MNL] &
		DRM_ELD_MNL_MASK) >> DRM_ELD_MNL_SHIFT;

	if (mnl > ELD_MAX_MNL) {
		dev_err(&hdev->dev, "HDMI: MNL Invalid %d\n", mnl);
		return -EINVAL;
	}

	port->eld.info.spk_alloc = port->eld.eld_buffer[DRM_ELD_SPEAKER];

	return 0;
}

static void hdac_hdmi_present_sense(struct hdac_hdmi_pin *pin,
				    struct hdac_hdmi_port *port)
{
	struct hdac_device *hdev = pin->hdev;
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	struct hdac_hdmi_pcm *pcm;
	int size = 0;
	int port_id = -1;

	if (!hdmi)
		return;

	/*
	 * In case of non MST pin, get_eld info API expectes port
	 * to be -1.
	 */
	mutex_lock(&hdmi->pin_mutex);
	port->eld.monitor_present = false;

	if (pin->mst_capable)
		port_id = port->id;

	size = snd_hdac_acomp_get_eld(hdev, pin->nid, port_id,
				&port->eld.monitor_present,
				port->eld.eld_buffer,
				ELD_MAX_SIZE);

	if (size > 0) {
		size = min(size, ELD_MAX_SIZE);
		if (hdac_hdmi_parse_eld(hdev, port) < 0)
			size = -EINVAL;
	}

	if (size > 0) {
		port->eld.eld_valid = true;
		port->eld.eld_size = size;
	} else {
		port->eld.eld_valid = false;
		port->eld.eld_size = 0;
	}

	pcm = hdac_hdmi_get_pcm(hdev, port);

	if (!port->eld.monitor_present || !port->eld.eld_valid) {

		dev_err(&hdev->dev, "%s: disconnect for pin:port %d:%d\n",
						__func__, pin->nid, port->id);

		/*
		 * PCMs are not registered during device probe, so don't
		 * report jack here. It will be done in usermode mux
		 * control select.
		 */
		if (pcm)
			hdac_hdmi_jack_report(pcm, port, false);

		mutex_unlock(&hdmi->pin_mutex);
		return;
	}

	if (port->eld.monitor_present && port->eld.eld_valid) {
		if (pcm)
			hdac_hdmi_jack_report(pcm, port, true);

		print_hex_dump_debug("ELD: ", DUMP_PREFIX_OFFSET, 16, 1,
			  port->eld.eld_buffer, port->eld.eld_size, false);

	}
	mutex_unlock(&hdmi->pin_mutex);
}

static int hdac_hdmi_add_ports(struct hdac_hdmi_priv *hdmi,
				struct hdac_hdmi_pin *pin)
{
	struct hdac_hdmi_port *ports;
	int max_ports = HDA_MAX_PORTS;
	int i;

	/*
	 * FIXME: max_port may vary for each platform, so pass this as
	 * as driver data or query from i915 interface when this API is
	 * implemented.
	 */

	ports = kcalloc(max_ports, sizeof(*ports), GFP_KERNEL);
	if (!ports)
		return -ENOMEM;

	for (i = 0; i < max_ports; i++) {
		ports[i].id = i;
		ports[i].pin = pin;
	}
	pin->ports = ports;
	pin->num_ports = max_ports;
	return 0;
}

static int hdac_hdmi_add_pin(struct hdac_device *hdev, hda_nid_t nid)
{
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	struct hdac_hdmi_pin *pin;
	int ret;

	pin = kzalloc(sizeof(*pin), GFP_KERNEL);
	if (!pin)
		return -ENOMEM;

	pin->nid = nid;
	pin->mst_capable = false;
	pin->hdev = hdev;
	ret = hdac_hdmi_add_ports(hdmi, pin);
	if (ret < 0)
		return ret;

	list_add_tail(&pin->head, &hdmi->pin_list);
	hdmi->num_pin++;
	hdmi->num_ports += pin->num_ports;

	return 0;
}

#define INTEL_VENDOR_NID 0x08
#define INTEL_GLK_VENDOR_NID 0x0b
#define INTEL_GET_VENDOR_VERB 0xf81
#define INTEL_SET_VENDOR_VERB 0x781
#define INTEL_EN_DP12			0x02 /* enable DP 1.2 features */
#define INTEL_EN_ALL_PIN_CVTS	0x01 /* enable 2nd & 3rd pins and convertors */

static void hdac_hdmi_skl_enable_all_pins(struct hdac_device *hdev)
{
	unsigned int vendor_param;
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	unsigned int vendor_nid = hdmi->drv_data->vendor_nid;

	vendor_param = snd_hdac_codec_read(hdev, vendor_nid, 0,
				INTEL_GET_VENDOR_VERB, 0);
	if (vendor_param == -1 || vendor_param & INTEL_EN_ALL_PIN_CVTS)
		return;

	vendor_param |= INTEL_EN_ALL_PIN_CVTS;
	vendor_param = snd_hdac_codec_read(hdev, vendor_nid, 0,
				INTEL_SET_VENDOR_VERB, vendor_param);
	if (vendor_param == -1)
		return;
}

static void hdac_hdmi_skl_enable_dp12(struct hdac_device *hdev)
{
	unsigned int vendor_param;
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	unsigned int vendor_nid = hdmi->drv_data->vendor_nid;

	vendor_param = snd_hdac_codec_read(hdev, vendor_nid, 0,
				INTEL_GET_VENDOR_VERB, 0);
	if (vendor_param == -1 || vendor_param & INTEL_EN_DP12)
		return;

	/* enable DP1.2 mode */
	vendor_param |= INTEL_EN_DP12;
	vendor_param = snd_hdac_codec_read(hdev, vendor_nid, 0,
				INTEL_SET_VENDOR_VERB, vendor_param);
	if (vendor_param == -1)
		return;

}

static const struct snd_soc_dai_ops hdmi_dai_ops = {
	.startup = hdac_hdmi_pcm_open,
	.shutdown = hdac_hdmi_pcm_close,
	.hw_params = hdac_hdmi_set_hw_params,
	.set_tdm_slot = hdac_hdmi_set_tdm_slot,
};

/*
 * Each converter can support a stream independently. So a dai is created
 * based on the number of converter queried.
 */
static int hdac_hdmi_create_dais(struct hdac_device *hdev,
		struct snd_soc_dai_driver **dais,
		struct hdac_hdmi_priv *hdmi, int num_dais)
{
	struct snd_soc_dai_driver *hdmi_dais;
	struct hdac_hdmi_cvt *cvt;
	char name[NAME_SIZE], dai_name[NAME_SIZE];
	int i = 0;
	u32 rates, bps;
	unsigned int rate_max = 384000, rate_min = 8000;
	u64 formats;
	int ret;

	hdmi_dais = devm_kzalloc(&hdev->dev,
			(sizeof(*hdmi_dais) * num_dais),
			GFP_KERNEL);
	if (!hdmi_dais)
		return -ENOMEM;

	list_for_each_entry(cvt, &hdmi->cvt_list, head) {
		ret = snd_hdac_query_supported_pcm(hdev, cvt->nid,
					&rates,	&formats, &bps);
		if (ret)
			return ret;

		/* Filter out 44.1, 88.2 and 176.4Khz */
		rates &= ~(SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_88200 |
			   SNDRV_PCM_RATE_176400);
		if (!rates)
			return -EINVAL;

		sprintf(dai_name, "intel-hdmi-hifi%d", i+1);
		hdmi_dais[i].name = devm_kstrdup(&hdev->dev,
					dai_name, GFP_KERNEL);

		if (!hdmi_dais[i].name)
			return -ENOMEM;

		snprintf(name, sizeof(name), "hifi%d", i+1);
		hdmi_dais[i].playback.stream_name =
				devm_kstrdup(&hdev->dev, name, GFP_KERNEL);
		if (!hdmi_dais[i].playback.stream_name)
			return -ENOMEM;

		/*
		 * Set caps based on capability queried from the converter.
		 * It will be constrained runtime based on ELD queried.
		 */
		hdmi_dais[i].playback.formats = formats;
		hdmi_dais[i].playback.rates = rates;
		hdmi_dais[i].playback.rate_max = rate_max;
		hdmi_dais[i].playback.rate_min = rate_min;
		hdmi_dais[i].playback.channels_min = 2;
		hdmi_dais[i].playback.channels_max = 2;
		hdmi_dais[i].playback.sig_bits = bps;
		hdmi_dais[i].ops = &hdmi_dai_ops;
		i++;
	}

	*dais = hdmi_dais;
	hdmi->dai_drv = hdmi_dais;

	return 0;
}

/*
 * Parse all nodes and store the cvt/pin nids in array
 * Add one time initialization for pin and cvt widgets
 */
static int hdac_hdmi_parse_and_map_nid(struct hdac_device *hdev,
		struct snd_soc_dai_driver **dais, int *num_dais)
{
	hda_nid_t nid;
	int i, num_nodes;
	struct hdac_hdmi_cvt *temp_cvt, *cvt_next;
	struct hdac_hdmi_pin *temp_pin, *pin_next;
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	int ret;

	hdac_hdmi_skl_enable_all_pins(hdev);
	hdac_hdmi_skl_enable_dp12(hdev);

	num_nodes = snd_hdac_get_sub_nodes(hdev, hdev->afg, &nid);
	if (!nid || num_nodes <= 0) {
		dev_warn(&hdev->dev, "HDMI: failed to get afg sub nodes\n");
		return -EINVAL;
	}

	for (i = 0; i < num_nodes; i++, nid++) {
		unsigned int caps;
		unsigned int type;

		caps = get_wcaps(hdev, nid);
		type = get_wcaps_type(caps);

		if (!(caps & AC_WCAP_DIGITAL))
			continue;

		switch (type) {

		case AC_WID_AUD_OUT:
			ret = hdac_hdmi_add_cvt(hdev, nid);
			if (ret < 0)
				goto free_widgets;
			break;

		case AC_WID_PIN:
			ret = hdac_hdmi_add_pin(hdev, nid);
			if (ret < 0)
				goto free_widgets;
			break;
		}
	}

	if (!hdmi->num_pin || !hdmi->num_cvt) {
		ret = -EIO;
		goto free_widgets;
	}

	ret = hdac_hdmi_create_dais(hdev, dais, hdmi, hdmi->num_cvt);
	if (ret) {
		dev_err(&hdev->dev, "Failed to create dais with err: %d\n",
							ret);
		goto free_widgets;
	}

	*num_dais = hdmi->num_cvt;
	ret = hdac_hdmi_init_dai_map(hdev);
	if (ret < 0)
		goto free_widgets;

	return ret;

free_widgets:
	list_for_each_entry_safe(temp_cvt, cvt_next, &hdmi->cvt_list, head) {
		list_del(&temp_cvt->head);
		kfree(temp_cvt->name);
		kfree(temp_cvt);
	}

	list_for_each_entry_safe(temp_pin, pin_next, &hdmi->pin_list, head) {
		for (i = 0; i < temp_pin->num_ports; i++)
			temp_pin->ports[i].pin = NULL;
		kfree(temp_pin->ports);
		list_del(&temp_pin->head);
		kfree(temp_pin);
	}

	return ret;
}

static int hdac_hdmi_pin2port(void *aptr, int pin)
{
	return pin - 4; /* map NID 0x05 -> port #1 */
}

static void hdac_hdmi_eld_notify_cb(void *aptr, int port, int pipe)
{
	struct hdac_device *hdev = aptr;
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	struct hdac_hdmi_pin *pin = NULL;
	struct hdac_hdmi_port *hport = NULL;
	struct snd_soc_component *component = hdmi->component;
	int i;

	/* Don't know how this mapping is derived */
	hda_nid_t pin_nid = port + 0x04;

	dev_dbg(&hdev->dev, "%s: for pin:%d port=%d\n", __func__,
							pin_nid, pipe);

	/*
	 * skip notification during system suspend (but not in runtime PM);
	 * the state will be updated at resume. Also since the ELD and
	 * connection states are updated in anyway at the end of the resume,
	 * we can skip it when received during PM process.
	 */
	if (snd_power_get_state(component->card->snd_card) !=
			SNDRV_CTL_POWER_D0)
		return;

	if (atomic_read(&hdev->in_pm))
		return;

	list_for_each_entry(pin, &hdmi->pin_list, head) {
		if (pin->nid != pin_nid)
			continue;

		/* In case of non MST pin, pipe is -1 */
		if (pipe == -1) {
			pin->mst_capable = false;
			/* if not MST, default is port[0] */
			hport = &pin->ports[0];
		} else {
			for (i = 0; i < pin->num_ports; i++) {
				pin->mst_capable = true;
				if (pin->ports[i].id == pipe) {
					hport = &pin->ports[i];
					break;
				}
			}
		}

		if (hport)
			hdac_hdmi_present_sense(pin, hport);
	}

}

static struct drm_audio_component_audio_ops aops = {
	.pin2port	= hdac_hdmi_pin2port,
	.pin_eld_notify	= hdac_hdmi_eld_notify_cb,
};

static struct snd_pcm *hdac_hdmi_get_pcm_from_id(struct snd_soc_card *card,
						int device)
{
	struct snd_soc_pcm_runtime *rtd;

	for_each_card_rtds(card, rtd) {
		if (rtd->pcm && (rtd->pcm->device == device))
			return rtd->pcm;
	}

	return NULL;
}

/* create jack pin kcontrols */
static int create_fill_jack_kcontrols(struct snd_soc_card *card,
				    struct hdac_device *hdev)
{
	struct hdac_hdmi_pin *pin;
	struct snd_kcontrol_new *kc;
	char kc_name[NAME_SIZE], xname[NAME_SIZE];
	char *name;
	int i = 0, j;
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	struct snd_soc_component *component = hdmi->component;

	kc = devm_kcalloc(component->dev, hdmi->num_ports,
				sizeof(*kc), GFP_KERNEL);

	if (!kc)
		return -ENOMEM;

	list_for_each_entry(pin, &hdmi->pin_list, head) {
		for (j = 0; j < pin->num_ports; j++) {
			snprintf(xname, sizeof(xname), "hif%d-%d Jack",
						pin->nid, pin->ports[j].id);
			name = devm_kstrdup(component->dev, xname, GFP_KERNEL);
			if (!name)
				return -ENOMEM;
			snprintf(kc_name, sizeof(kc_name), "%s Switch", xname);
			kc[i].name = devm_kstrdup(component->dev, kc_name,
							GFP_KERNEL);
			if (!kc[i].name)
				return -ENOMEM;

			kc[i].private_value = (unsigned long)name;
			kc[i].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
			kc[i].access = 0;
			kc[i].info = snd_soc_dapm_info_pin_switch;
			kc[i].put = snd_soc_dapm_put_pin_switch;
			kc[i].get = snd_soc_dapm_get_pin_switch;
			i++;
		}
	}

	return snd_soc_add_card_controls(card, kc, i);
}

int hdac_hdmi_jack_port_init(struct snd_soc_component *component,
			struct snd_soc_dapm_context *dapm)
{
	struct hdac_hdmi_priv *hdmi = snd_soc_component_get_drvdata(component);
	struct hdac_device *hdev = hdmi->hdev;
	struct hdac_hdmi_pin *pin;
	struct snd_soc_dapm_widget *widgets;
	struct snd_soc_dapm_route *route;
	char w_name[NAME_SIZE];
	int i = 0, j, ret;

	widgets = devm_kcalloc(dapm->dev, hdmi->num_ports,
				sizeof(*widgets), GFP_KERNEL);

	if (!widgets)
		return -ENOMEM;

	route = devm_kcalloc(dapm->dev, hdmi->num_ports,
				sizeof(*route), GFP_KERNEL);
	if (!route)
		return -ENOMEM;

	/* create Jack DAPM widget */
	list_for_each_entry(pin, &hdmi->pin_list, head) {
		for (j = 0; j < pin->num_ports; j++) {
			snprintf(w_name, sizeof(w_name), "hif%d-%d Jack",
						pin->nid, pin->ports[j].id);

			ret = hdac_hdmi_fill_widget_info(dapm->dev, &widgets[i],
					snd_soc_dapm_spk, NULL,
					w_name, NULL, NULL, 0, NULL, 0);
			if (ret < 0)
				return ret;

			pin->ports[j].jack_pin = widgets[i].name;
			pin->ports[j].dapm = dapm;

			/* add to route from Jack widget to output */
			hdac_hdmi_fill_route(&route[i], pin->ports[j].jack_pin,
					NULL, pin->ports[j].output_pin, NULL);

			i++;
		}
	}

	/* Add Route from Jack widget to the output widget */
	ret = snd_soc_dapm_new_controls(dapm, widgets, hdmi->num_ports);
	if (ret < 0)
		return ret;

	ret = snd_soc_dapm_add_routes(dapm, route, hdmi->num_ports);
	if (ret < 0)
		return ret;

	ret = snd_soc_dapm_new_widgets(dapm->card);
	if (ret < 0)
		return ret;

	/* Add Jack Pin switch Kcontrol */
	ret = create_fill_jack_kcontrols(dapm->card, hdev);

	if (ret < 0)
		return ret;

	/* default set the Jack Pin switch to OFF */
	list_for_each_entry(pin, &hdmi->pin_list, head) {
		for (j = 0; j < pin->num_ports; j++)
			snd_soc_dapm_disable_pin(pin->ports[j].dapm,
						pin->ports[j].jack_pin);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(hdac_hdmi_jack_port_init);

int hdac_hdmi_jack_init(struct snd_soc_dai *dai, int device,
				struct snd_soc_jack *jack)
{
	struct snd_soc_component *component = dai->component;
	struct hdac_hdmi_priv *hdmi = snd_soc_component_get_drvdata(component);
	struct hdac_device *hdev = hdmi->hdev;
	struct hdac_hdmi_pcm *pcm;
	struct snd_pcm *snd_pcm;
	int err;

	/*
	 * this is a new PCM device, create new pcm and
	 * add to the pcm list
	 */
	pcm = kzalloc(sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return -ENOMEM;
	pcm->pcm_id = device;
	pcm->cvt = hdmi->dai_map[dai->id].cvt;
	pcm->jack_event = 0;
	pcm->jack = jack;
	mutex_init(&pcm->lock);
	INIT_LIST_HEAD(&pcm->port_list);
	snd_pcm = hdac_hdmi_get_pcm_from_id(dai->component->card, device);
	if (snd_pcm) {
		err = snd_hdac_add_chmap_ctls(snd_pcm, device, &hdmi->chmap);
		if (err < 0) {
			dev_err(&hdev->dev,
				"chmap control add failed with err: %d for pcm: %d\n",
				err, device);
			kfree(pcm);
			return err;
		}
	}

	list_add_tail(&pcm->head, &hdmi->pcm_list);

	return 0;
}
EXPORT_SYMBOL_GPL(hdac_hdmi_jack_init);

static void hdac_hdmi_present_sense_all_pins(struct hdac_device *hdev,
			struct hdac_hdmi_priv *hdmi, bool detect_pin_caps)
{
	int i;
	struct hdac_hdmi_pin *pin;

	list_for_each_entry(pin, &hdmi->pin_list, head) {
		if (detect_pin_caps) {

			if (hdac_hdmi_get_port_len(hdev, pin->nid)  == 0)
				pin->mst_capable = false;
			else
				pin->mst_capable = true;
		}

		for (i = 0; i < pin->num_ports; i++) {
			if (!pin->mst_capable && i > 0)
				continue;

			hdac_hdmi_present_sense(pin, &pin->ports[i]);
		}
	}
}

static int hdmi_codec_probe(struct snd_soc_component *component)
{
	struct hdac_hdmi_priv *hdmi = snd_soc_component_get_drvdata(component);
	struct hdac_device *hdev = hdmi->hdev;
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	struct hdac_ext_link *hlink = NULL;
	int ret;

	hdmi->component = component;

	/*
	 * hold the ref while we probe, also no need to drop the ref on
	 * exit, we call pm_runtime_suspend() so that will do for us
	 */
	hlink = snd_hdac_ext_bus_get_link(hdev->bus, dev_name(&hdev->dev));
	if (!hlink) {
		dev_err(&hdev->dev, "hdac link not found\n");
		return -EIO;
	}

	snd_hdac_ext_bus_link_get(hdev->bus, hlink);

	ret = create_fill_widget_route_map(dapm);
	if (ret < 0)
		return ret;

	aops.audio_ptr = hdev;
	ret = snd_hdac_acomp_register_notifier(hdev->bus, &aops);
	if (ret < 0) {
		dev_err(&hdev->dev, "notifier register failed: err: %d\n", ret);
		return ret;
	}

	hdac_hdmi_present_sense_all_pins(hdev, hdmi, true);
	/* Imp: Store the card pointer in hda_codec */
	hdmi->card = dapm->card->snd_card;

	/*
	 * hdac_device core already sets the state to active and calls
	 * get_noresume. So enable runtime and set the device to suspend.
	 */
	pm_runtime_enable(&hdev->dev);
	pm_runtime_put(&hdev->dev);
	pm_runtime_suspend(&hdev->dev);

	return 0;
}

static void hdmi_codec_remove(struct snd_soc_component *component)
{
	struct hdac_hdmi_priv *hdmi = snd_soc_component_get_drvdata(component);
	struct hdac_device *hdev = hdmi->hdev;

	pm_runtime_disable(&hdev->dev);
}

#ifdef CONFIG_PM
static int hdmi_codec_prepare(struct device *dev)
{
	struct hdac_device *hdev = dev_to_hdac_dev(dev);

	pm_runtime_get_sync(&hdev->dev);

	/*
	 * Power down afg.
	 * codec_read is preferred over codec_write to set the power state.
	 * This way verb is send to set the power state and response
	 * is received. So setting power state is ensured without using loop
	 * to read the state.
	 */
	snd_hdac_codec_read(hdev, hdev->afg, 0,	AC_VERB_SET_POWER_STATE,
							AC_PWRST_D3);

	return 0;
}

static void hdmi_codec_complete(struct device *dev)
{
	struct hdac_device *hdev = dev_to_hdac_dev(dev);
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);

	/* Power up afg */
	snd_hdac_codec_read(hdev, hdev->afg, 0,	AC_VERB_SET_POWER_STATE,
							AC_PWRST_D0);

	hdac_hdmi_skl_enable_all_pins(hdev);
	hdac_hdmi_skl_enable_dp12(hdev);

	/*
	 * As the ELD notify callback request is not entertained while the
	 * device is in suspend state. Need to manually check detection of
	 * all pins here. pin capablity change is not support, so use the
	 * already set pin caps.
	 */
	hdac_hdmi_present_sense_all_pins(hdev, hdmi, false);

	pm_runtime_put_sync(&hdev->dev);
}
#else
#define hdmi_codec_prepare NULL
#define hdmi_codec_complete NULL
#endif

static const struct snd_soc_component_driver hdmi_hda_codec = {
	.probe			= hdmi_codec_probe,
	.remove			= hdmi_codec_remove,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static void hdac_hdmi_get_chmap(struct hdac_device *hdev, int pcm_idx,
					unsigned char *chmap)
{
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	struct hdac_hdmi_pcm *pcm = get_hdmi_pcm_from_id(hdmi, pcm_idx);

	memcpy(chmap, pcm->chmap, ARRAY_SIZE(pcm->chmap));
}

static void hdac_hdmi_set_chmap(struct hdac_device *hdev, int pcm_idx,
				unsigned char *chmap, int prepared)
{
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	struct hdac_hdmi_pcm *pcm = get_hdmi_pcm_from_id(hdmi, pcm_idx);
	struct hdac_hdmi_port *port;

	if (!pcm)
		return;

	if (list_empty(&pcm->port_list))
		return;

	mutex_lock(&pcm->lock);
	pcm->chmap_set = true;
	memcpy(pcm->chmap, chmap, ARRAY_SIZE(pcm->chmap));
	list_for_each_entry(port, &pcm->port_list, head)
		if (prepared)
			hdac_hdmi_setup_audio_infoframe(hdev, pcm, port);
	mutex_unlock(&pcm->lock);
}

static bool is_hdac_hdmi_pcm_attached(struct hdac_device *hdev, int pcm_idx)
{
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	struct hdac_hdmi_pcm *pcm = get_hdmi_pcm_from_id(hdmi, pcm_idx);

	if (!pcm)
		return false;

	if (list_empty(&pcm->port_list))
		return false;

	return true;
}

static int hdac_hdmi_get_spk_alloc(struct hdac_device *hdev, int pcm_idx)
{
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	struct hdac_hdmi_pcm *pcm = get_hdmi_pcm_from_id(hdmi, pcm_idx);
	struct hdac_hdmi_port *port;

	if (!pcm)
		return 0;

	if (list_empty(&pcm->port_list))
		return 0;

	port = list_first_entry(&pcm->port_list, struct hdac_hdmi_port, head);

	if (!port || !port->eld.eld_valid)
		return 0;

	return port->eld.info.spk_alloc;
}

static struct hdac_hdmi_drv_data intel_glk_drv_data  = {
	.vendor_nid = INTEL_GLK_VENDOR_NID,
};

static struct hdac_hdmi_drv_data intel_drv_data  = {
	.vendor_nid = INTEL_VENDOR_NID,
};

static int hdac_hdmi_dev_probe(struct hdac_device *hdev)
{
	struct hdac_hdmi_priv *hdmi_priv = NULL;
	struct snd_soc_dai_driver *hdmi_dais = NULL;
	struct hdac_ext_link *hlink = NULL;
	int num_dais = 0;
	int ret = 0;
	struct hdac_driver *hdrv = drv_to_hdac_driver(hdev->dev.driver);
	const struct hda_device_id *hdac_id = hdac_get_device_id(hdev, hdrv);

	/* hold the ref while we probe */
	hlink = snd_hdac_ext_bus_get_link(hdev->bus, dev_name(&hdev->dev));
	if (!hlink) {
		dev_err(&hdev->dev, "hdac link not found\n");
		return -EIO;
	}

	snd_hdac_ext_bus_link_get(hdev->bus, hlink);

	hdmi_priv = devm_kzalloc(&hdev->dev, sizeof(*hdmi_priv), GFP_KERNEL);
	if (hdmi_priv == NULL)
		return -ENOMEM;

	snd_hdac_register_chmap_ops(hdev, &hdmi_priv->chmap);
	hdmi_priv->chmap.ops.get_chmap = hdac_hdmi_get_chmap;
	hdmi_priv->chmap.ops.set_chmap = hdac_hdmi_set_chmap;
	hdmi_priv->chmap.ops.is_pcm_attached = is_hdac_hdmi_pcm_attached;
	hdmi_priv->chmap.ops.get_spk_alloc = hdac_hdmi_get_spk_alloc;
	hdmi_priv->hdev = hdev;

	if (!hdac_id)
		return -ENODEV;

	if (hdac_id->driver_data)
		hdmi_priv->drv_data =
			(struct hdac_hdmi_drv_data *)hdac_id->driver_data;
	else
		hdmi_priv->drv_data = &intel_drv_data;

	dev_set_drvdata(&hdev->dev, hdmi_priv);

	INIT_LIST_HEAD(&hdmi_priv->pin_list);
	INIT_LIST_HEAD(&hdmi_priv->cvt_list);
	INIT_LIST_HEAD(&hdmi_priv->pcm_list);
	mutex_init(&hdmi_priv->pin_mutex);

	/*
	 * Turned off in the runtime_suspend during the first explicit
	 * pm_runtime_suspend call.
	 */
	ret = snd_hdac_display_power(hdev->bus, true);
	if (ret < 0) {
		dev_err(&hdev->dev,
			"Cannot turn on display power on i915 err: %d\n",
			ret);
		return ret;
	}

	ret = hdac_hdmi_parse_and_map_nid(hdev, &hdmi_dais, &num_dais);
	if (ret < 0) {
		dev_err(&hdev->dev,
			"Failed in parse and map nid with err: %d\n", ret);
		return ret;
	}
	snd_hdac_refresh_widgets(hdev, true);

	/* ASoC specific initialization */
	ret = devm_snd_soc_register_component(&hdev->dev, &hdmi_hda_codec,
					hdmi_dais, num_dais);

	snd_hdac_ext_bus_link_put(hdev->bus, hlink);

	return ret;
}

static int hdac_hdmi_dev_remove(struct hdac_device *hdev)
{
	struct hdac_hdmi_priv *hdmi = hdev_to_hdmi_priv(hdev);
	struct hdac_hdmi_pin *pin, *pin_next;
	struct hdac_hdmi_cvt *cvt, *cvt_next;
	struct hdac_hdmi_pcm *pcm, *pcm_next;
	struct hdac_hdmi_port *port, *port_next;
	int i;

	list_for_each_entry_safe(pcm, pcm_next, &hdmi->pcm_list, head) {
		pcm->cvt = NULL;
		if (list_empty(&pcm->port_list))
			continue;

		list_for_each_entry_safe(port, port_next,
					&pcm->port_list, head)
			list_del(&port->head);

		list_del(&pcm->head);
		kfree(pcm);
	}

	list_for_each_entry_safe(cvt, cvt_next, &hdmi->cvt_list, head) {
		list_del(&cvt->head);
		kfree(cvt->name);
		kfree(cvt);
	}

	list_for_each_entry_safe(pin, pin_next, &hdmi->pin_list, head) {
		for (i = 0; i < pin->num_ports; i++)
			pin->ports[i].pin = NULL;
		kfree(pin->ports);
		list_del(&pin->head);
		kfree(pin);
	}

	return 0;
}

#ifdef CONFIG_PM
/*
 * Power management sequences
 * ==========================
 *
 * The following explains the PM handling of HDAC HDMI with its parent
 * device SKL and display power usage
 *
 * Probe
 * -----
 * In SKL probe,
 * 1. skl_probe_work() powers up the display (refcount++ -> 1)
 * 2. enumerates the codecs on the link
 * 3. powers down the display  (refcount-- -> 0)
 *
 * In HDAC HDMI probe,
 * 1. hdac_hdmi_dev_probe() powers up the display (refcount++ -> 1)
 * 2. probe the codec
 * 3. put the HDAC HDMI device to runtime suspend
 * 4. hdac_hdmi_runtime_suspend() powers down the display (refcount-- -> 0)
 *
 * Once children are runtime suspended, SKL device also goes to runtime
 * suspend
 *
 * HDMI Playback
 * -------------
 * Open HDMI device,
 * 1. skl_runtime_resume() invoked
 * 2. hdac_hdmi_runtime_resume() powers up the display (refcount++ -> 1)
 *
 * Close HDMI device,
 * 1. hdac_hdmi_runtime_suspend() powers down the display (refcount-- -> 0)
 * 2. skl_runtime_suspend() invoked
 *
 * S0/S3 Cycle with playback in progress
 * -------------------------------------
 * When the device is opened for playback, the device is runtime active
 * already and the display refcount is 1 as explained above.
 *
 * Entering to S3,
 * 1. hdmi_codec_prepare() invoke the runtime resume of codec which just
 *    increments the PM runtime usage count of the codec since the device
 *    is in use already
 * 2. skl_suspend() powers down the display (refcount-- -> 0)
 *
 * Wakeup from S3,
 * 1. skl_resume() powers up the display (refcount++ -> 1)
 * 2. hdmi_codec_complete() invokes the runtime suspend of codec which just
 *    decrements the PM runtime usage count of the codec since the device
 *    is in use already
 *
 * Once playback is stopped, the display refcount is set to 0 as explained
 * above in the HDMI playback sequence. The PM handlings are designed in
 * such way that to balance the refcount of display power when the codec
 * device put to S3 while playback is going on.
 *
 * S0/S3 Cycle without playback in progress
 * ----------------------------------------
 * Entering to S3,
 * 1. hdmi_codec_prepare() invoke the runtime resume of codec
 * 2. skl_runtime_resume() invoked
 * 3. hdac_hdmi_runtime_resume() powers up the display (refcount++ -> 1)
 * 4. skl_suspend() powers down the display (refcount-- -> 0)
 *
 * Wakeup from S3,
 * 1. skl_resume() powers up the display (refcount++ -> 1)
 * 2. hdmi_codec_complete() invokes the runtime suspend of codec
 * 3. hdac_hdmi_runtime_suspend() powers down the display (refcount-- -> 0)
 * 4. skl_runtime_suspend() invoked
 */
static int hdac_hdmi_runtime_suspend(struct device *dev)
{
	struct hdac_device *hdev = dev_to_hdac_dev(dev);
	struct hdac_bus *bus = hdev->bus;
	struct hdac_ext_link *hlink = NULL;
	int err;

	dev_dbg(dev, "Enter: %s\n", __func__);

	/* controller may not have been initialized for the first time */
	if (!bus)
		return 0;

	/*
	 * Power down afg.
	 * codec_read is preferred over codec_write to set the power state.
	 * This way verb is send to set the power state and response
	 * is received. So setting power state is ensured without using loop
	 * to read the state.
	 */
	snd_hdac_codec_read(hdev, hdev->afg, 0,	AC_VERB_SET_POWER_STATE,
							AC_PWRST_D3);

	hlink = snd_hdac_ext_bus_get_link(bus, dev_name(dev));
	if (!hlink) {
		dev_err(dev, "hdac link not found\n");
		return -EIO;
	}

	snd_hdac_ext_bus_link_put(bus, hlink);

	err = snd_hdac_display_power(bus, false);
	if (err < 0)
		dev_err(dev, "Cannot turn off display power on i915\n");

	return err;
}

static int hdac_hdmi_runtime_resume(struct device *dev)
{
	struct hdac_device *hdev = dev_to_hdac_dev(dev);
	struct hdac_bus *bus = hdev->bus;
	struct hdac_ext_link *hlink = NULL;
	int err;

	dev_dbg(dev, "Enter: %s\n", __func__);

	/* controller may not have been initialized for the first time */
	if (!bus)
		return 0;

	hlink = snd_hdac_ext_bus_get_link(bus, dev_name(dev));
	if (!hlink) {
		dev_err(dev, "hdac link not found\n");
		return -EIO;
	}

	snd_hdac_ext_bus_link_get(bus, hlink);

	err = snd_hdac_display_power(bus, true);
	if (err < 0) {
		dev_err(dev, "Cannot turn on display power on i915\n");
		return err;
	}

	hdac_hdmi_skl_enable_all_pins(hdev);
	hdac_hdmi_skl_enable_dp12(hdev);

	/* Power up afg */
	snd_hdac_codec_read(hdev, hdev->afg, 0,	AC_VERB_SET_POWER_STATE,
							AC_PWRST_D0);

	return 0;
}
#else
#define hdac_hdmi_runtime_suspend NULL
#define hdac_hdmi_runtime_resume NULL
#endif

static const struct dev_pm_ops hdac_hdmi_pm = {
	SET_RUNTIME_PM_OPS(hdac_hdmi_runtime_suspend, hdac_hdmi_runtime_resume, NULL)
	.prepare = hdmi_codec_prepare,
	.complete = hdmi_codec_complete,
};

static const struct hda_device_id hdmi_list[] = {
	HDA_CODEC_EXT_ENTRY(0x80862809, 0x100000, "Skylake HDMI", 0),
	HDA_CODEC_EXT_ENTRY(0x8086280a, 0x100000, "Broxton HDMI", 0),
	HDA_CODEC_EXT_ENTRY(0x8086280b, 0x100000, "Kabylake HDMI", 0),
	HDA_CODEC_EXT_ENTRY(0x8086280c, 0x100000, "Cannonlake HDMI",
						   &intel_glk_drv_data),
	HDA_CODEC_EXT_ENTRY(0x8086280d, 0x100000, "Geminilake HDMI",
						   &intel_glk_drv_data),
	{}
};

MODULE_DEVICE_TABLE(hdaudio, hdmi_list);

static struct hdac_driver hdmi_driver = {
	.driver = {
		.name   = "HDMI HDA Codec",
		.pm = &hdac_hdmi_pm,
	},
	.id_table       = hdmi_list,
	.probe          = hdac_hdmi_dev_probe,
	.remove         = hdac_hdmi_dev_remove,
};

static int __init hdmi_init(void)
{
	return snd_hda_ext_driver_register(&hdmi_driver);
}

static void __exit hdmi_exit(void)
{
	snd_hda_ext_driver_unregister(&hdmi_driver);
}

module_init(hdmi_init);
module_exit(hdmi_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("HDMI HD codec");
MODULE_AUTHOR("Samreen Nilofer<samreen.nilofer@intel.com>");
MODULE_AUTHOR("Subhransu S. Prusty<subhransu.s.prusty@intel.com>");
