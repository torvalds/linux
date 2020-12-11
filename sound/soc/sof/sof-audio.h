/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2019 Intel Corporation. All rights reserved.
 *
 * Author: Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
 */

#ifndef __SOUND_SOC_SOF_AUDIO_H
#define __SOUND_SOC_SOF_AUDIO_H

#include <linux/workqueue.h>

#include <sound/soc.h>
#include <sound/control.h>
#include <sound/sof/stream.h> /* needs to be included before control.h */
#include <sound/sof/control.h>
#include <sound/sof/dai.h>
#include <sound/sof/topology.h>
#include "sof-priv.h"

#define SOF_AUDIO_PCM_DRV_NAME	"sof-audio-component"

/* max number of FE PCMs before BEs */
#define SOF_BE_PCM_BASE		16

#define DMA_CHAN_INVALID	0xFFFFFFFF

/* PCM stream, mapped to FW component  */
struct snd_sof_pcm_stream {
	u32 comp_id;
	struct snd_dma_buffer page_table;
	struct sof_ipc_stream_posn posn;
	struct snd_pcm_substream *substream;
	struct work_struct period_elapsed_work;
	bool d0i3_compatible; /* DSP can be in D0I3 when this pcm is opened */
	/*
	 * flag to indicate that the DSP pipelines should be kept
	 * active or not while suspending the stream
	 */
	bool suspend_ignored;
};

/* ALSA SOF PCM device */
struct snd_sof_pcm {
	struct snd_soc_component *scomp;
	struct snd_soc_tplg_pcm pcm;
	struct snd_sof_pcm_stream stream[2];
	struct list_head list;	/* list in sdev pcm list */
	struct snd_pcm_hw_params params[2];
	bool prepared[2]; /* PCM_PARAMS set successfully */
};

struct snd_sof_led_control {
	unsigned int use_led;
	unsigned int direction;
	int led_value;
};

/* ALSA SOF Kcontrol device */
struct snd_sof_control {
	struct snd_soc_component *scomp;
	int comp_id;
	int min_volume_step; /* min volume step for volume_table */
	int max_volume_step; /* max volume step for volume_table */
	int num_channels;
	u32 readback_offset; /* offset to mmapped data if used */
	struct sof_ipc_ctrl_data *control_data;
	u32 size;	/* cdata size */
	enum sof_ipc_ctrl_cmd cmd;
	u32 *volume_table; /* volume table computed from tlv data*/

	struct list_head list;	/* list in sdev control list */

	struct snd_sof_led_control led_ctl;
};

/* ASoC SOF DAPM widget */
struct snd_sof_widget {
	struct snd_soc_component *scomp;
	int comp_id;
	int pipeline_id;
	int complete;
	int core;
	int id;

	struct snd_soc_dapm_widget *widget;
	struct list_head list;	/* list in sdev widget list */

	/* extended data for UUID components */
	struct sof_ipc_comp_ext comp_ext;

	void *private;		/* core does not touch this */
};

/* ASoC SOF DAPM route */
struct snd_sof_route {
	struct snd_soc_component *scomp;

	struct snd_soc_dapm_route *route;
	struct list_head list;	/* list in sdev route list */

	void *private;
};

/* ASoC DAI device */
struct snd_sof_dai {
	struct snd_soc_component *scomp;
	const char *name;
	const char *cpu_dai_name;

	struct sof_ipc_comp_dai comp_dai;
	struct sof_ipc_dai_config *dai_config;
	struct list_head list;	/* list in sdev dai list */
};

/*
 * Kcontrols.
 */

int snd_sof_volume_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol);
int snd_sof_volume_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol);
int snd_sof_switch_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol);
int snd_sof_switch_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol);
int snd_sof_enum_get(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_value *ucontrol);
int snd_sof_enum_put(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_value *ucontrol);
int snd_sof_bytes_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol);
int snd_sof_bytes_put(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol);
int snd_sof_bytes_ext_put(struct snd_kcontrol *kcontrol,
			  const unsigned int __user *binary_data,
			  unsigned int size);
int snd_sof_bytes_ext_get(struct snd_kcontrol *kcontrol,
			  unsigned int __user *binary_data,
			  unsigned int size);
int snd_sof_bytes_ext_volatile_get(struct snd_kcontrol *kcontrol, unsigned int __user *binary_data,
				   unsigned int size);

/*
 * Topology.
 * There is no snd_sof_free_topology since topology components will
 * be freed by snd_soc_unregister_component,
 */
int snd_sof_load_topology(struct snd_soc_component *scomp, const char *file);
int snd_sof_complete_pipeline(struct device *dev,
			      struct snd_sof_widget *swidget);

int sof_load_pipeline_ipc(struct device *dev,
			  struct sof_ipc_pipe_new *pipeline,
			  struct sof_ipc_comp_reply *r);
int sof_pipeline_core_enable(struct snd_sof_dev *sdev,
			     const struct snd_sof_widget *swidget);

/*
 * Stream IPC
 */
int snd_sof_ipc_stream_posn(struct snd_soc_component *scomp,
			    struct snd_sof_pcm *spcm, int direction,
			    struct sof_ipc_stream_posn *posn);

struct snd_sof_widget *snd_sof_find_swidget(struct snd_soc_component *scomp,
					    const char *name);
struct snd_sof_widget *
snd_sof_find_swidget_sname(struct snd_soc_component *scomp,
			   const char *pcm_name, int dir);
struct snd_sof_dai *snd_sof_find_dai(struct snd_soc_component *scomp,
				     const char *name);

static inline
struct snd_sof_pcm *snd_sof_find_spcm_dai(struct snd_soc_component *scomp,
					  struct snd_soc_pcm_runtime *rtd)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);

	struct snd_sof_pcm *spcm = NULL;

	list_for_each_entry(spcm, &sdev->pcm_list, list) {
		if (le32_to_cpu(spcm->pcm.dai_id) == rtd->dai_link->id)
			return spcm;
	}

	return NULL;
}

struct snd_sof_pcm *snd_sof_find_spcm_name(struct snd_soc_component *scomp,
					   const char *name);
struct snd_sof_pcm *snd_sof_find_spcm_comp(struct snd_soc_component *scomp,
					   unsigned int comp_id,
					   int *direction);
struct snd_sof_pcm *snd_sof_find_spcm_pcm_id(struct snd_soc_component *scomp,
					     unsigned int pcm_id);
const struct sof_ipc_pipe_new *snd_sof_pipeline_find(struct snd_sof_dev *sdev,
						     int pipeline_id);
void snd_sof_pcm_period_elapsed(struct snd_pcm_substream *substream);
void snd_sof_pcm_period_elapsed_work(struct work_struct *work);

/*
 * Mixer IPC
 */
int snd_sof_ipc_set_get_comp_data(struct snd_sof_control *scontrol,
				  u32 ipc_cmd,
				  enum sof_ipc_ctrl_type ctrl_type,
				  enum sof_ipc_ctrl_cmd ctrl_cmd,
				  bool send);

/* PM */
int sof_restore_pipelines(struct device *dev);
int sof_set_hw_params_upon_resume(struct device *dev);
bool snd_sof_stream_suspend_ignored(struct snd_sof_dev *sdev);
bool snd_sof_dsp_only_d0i3_compatible_stream_active(struct snd_sof_dev *sdev);

/* Machine driver enumeration */
int sof_machine_register(struct snd_sof_dev *sdev, void *pdata);
void sof_machine_unregister(struct snd_sof_dev *sdev, void *pdata);

#endif
