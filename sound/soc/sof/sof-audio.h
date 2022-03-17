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

#define WIDGET_IS_DAI(id) ((id) == snd_soc_dapm_dai_in || (id) == snd_soc_dapm_dai_out)

/*
 * Volume fractional word length define to 16 sets
 * the volume linear gain value to use Qx.16 format
 */
#define VOLUME_FWL	16

struct snd_sof_widget;
struct snd_sof_route;
struct snd_sof_control;

struct snd_sof_dai_config_data {
	int dai_index;
	int dai_data; /* contains DAI-specific information */
};

/**
 * struct sof_ipc_pcm_ops - IPC-specific PCM ops
 * @hw_params: Function pointer for hw_params
 * @hw_free: Function pointer for hw_free
 * @trigger: Function pointer for trigger
 * @dai_link_fixup: Function pointer for DAI link fixup
 */
struct sof_ipc_pcm_ops {
	int (*hw_params)(struct snd_soc_component *component, struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params,
			 struct snd_sof_platform_stream_params *platform_params);
	int (*hw_free)(struct snd_soc_component *component, struct snd_pcm_substream *substream);
	int (*trigger)(struct snd_soc_component *component,  struct snd_pcm_substream *substream,
		       int cmd);
	int (*dai_link_fixup)(struct snd_soc_pcm_runtime *rtd, struct snd_pcm_hw_params *params);
};

/**
 * struct sof_ipc_tplg_control_ops - IPC-specific ops for topology kcontrol IO
 */
struct sof_ipc_tplg_control_ops {
	bool (*volume_put)(struct snd_sof_control *scontrol, struct snd_ctl_elem_value *ucontrol);
	int (*volume_get)(struct snd_sof_control *scontrol, struct snd_ctl_elem_value *ucontrol);
	bool (*switch_put)(struct snd_sof_control *scontrol, struct snd_ctl_elem_value *ucontrol);
	int (*switch_get)(struct snd_sof_control *scontrol, struct snd_ctl_elem_value *ucontrol);
	bool (*enum_put)(struct snd_sof_control *scontrol, struct snd_ctl_elem_value *ucontrol);
	int (*enum_get)(struct snd_sof_control *scontrol, struct snd_ctl_elem_value *ucontrol);
	int (*bytes_put)(struct snd_sof_control *scontrol, struct snd_ctl_elem_value *ucontrol);
	int (*bytes_get)(struct snd_sof_control *scontrol, struct snd_ctl_elem_value *ucontrol);
	int (*bytes_ext_get)(struct snd_sof_control *scontrol,
			     const unsigned int __user *binary_data, unsigned int size);
	int (*bytes_ext_volatile_get)(struct snd_sof_control *scontrol,
				      const unsigned int __user *binary_data, unsigned int size);
	int (*bytes_ext_put)(struct snd_sof_control *scontrol,
			     const unsigned int __user *binary_data, unsigned int size);
	/* update control data based on notification from the DSP */
	void (*update)(struct snd_sof_dev *sdev, void *ipc_control_message);
};

/**
 * struct sof_ipc_tplg_widget_ops - IPC-specific ops for topology widgets
 * @ipc_setup: Function pointer for setting up widget IPC params
 * @ipc_free: Function pointer for freeing widget IPC params
 * @token_list: List of token ID's that should be parsed for the widget
 * @token_list_size: number of elements in token_list
 * @bind_event: Function pointer for binding events to the widget
 */
struct sof_ipc_tplg_widget_ops {
	int (*ipc_setup)(struct snd_sof_widget *swidget);
	void (*ipc_free)(struct snd_sof_widget *swidget);
	enum sof_tokens *token_list;
	int token_list_size;
	int (*bind_event)(struct snd_soc_component *scomp, struct snd_sof_widget *swidget,
			  u16 event_type);
};

/**
 * struct sof_ipc_tplg_ops - IPC-specific topology ops
 * @widget: Array of pointers to IPC-specific ops for widgets. This should always be of size
 *	    SND_SOF_DAPM_TYPE_COUNT i.e one per widget type. Unsupported widget types will be
 *	    initialized to 0.
 * @control: Pointer to the IPC-specific ops for topology kcontrol IO
 * @route_setup: Function pointer for setting up pipeline connections
 * @token_list: List of all tokens supported by the IPC version. The size of the token_list
 *		array should be SOF_TOKEN_COUNT. The unused elements in the array will be
 *		initialized to 0.
 * @control_setup: Function pointer for setting up kcontrol IPC-specific data
 * @control_free: Function pointer for freeing kcontrol IPC-specific data
 * @pipeline_complete: Function pointer for pipeline complete IPC
 * @widget_setup: Function pointer for setting up setup in the DSP
 * @widget_free: Function pointer for freeing widget in the DSP
 * @dai_config: Function pointer for sending DAI config IPC to the DSP
 */
struct sof_ipc_tplg_ops {
	const struct sof_ipc_tplg_widget_ops *widget;
	const struct sof_ipc_tplg_control_ops *control;
	int (*route_setup)(struct snd_sof_dev *sdev, struct snd_sof_route *sroute);
	const struct sof_token_info *token_list;
	int (*control_setup)(struct snd_sof_dev *sdev, struct snd_sof_control *scontrol);
	int (*control_free)(struct snd_sof_dev *sdev, struct snd_sof_control *scontrol);
	int (*pipeline_complete)(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget);
	int (*widget_setup)(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget);
	int (*widget_free)(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget);
	int (*dai_config)(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
			  unsigned int flags, struct snd_sof_dai_config_data *data);
};

/** struct snd_sof_tuple - Tuple info
 * @token:	Token ID
 * @value:	union of a string or a u32 values
 */
struct snd_sof_tuple {
	u32 token;
	union {
		u32 v;
		const char *s;
	} value;
};

/*
 * List of SOF token ID's. The order of ID's does not matter as token arrays are looked up based on
 * the ID.
 */
enum sof_tokens {
	SOF_PCM_TOKENS,
	SOF_PIPELINE_TOKENS,
	SOF_SCHED_TOKENS,
	SOF_ASRC_TOKENS,
	SOF_SRC_TOKENS,
	SOF_COMP_TOKENS,
	SOF_BUFFER_TOKENS,
	SOF_VOLUME_TOKENS,
	SOF_PROCESS_TOKENS,
	SOF_DAI_TOKENS,
	SOF_DAI_LINK_TOKENS,
	SOF_HDA_TOKENS,
	SOF_SSP_TOKENS,
	SOF_ALH_TOKENS,
	SOF_DMIC_TOKENS,
	SOF_DMIC_PDM_TOKENS,
	SOF_ESAI_TOKENS,
	SOF_SAI_TOKENS,
	SOF_AFE_TOKENS,
	SOF_CORE_TOKENS,
	SOF_COMP_EXT_TOKENS,

	/* this should be the last */
	SOF_TOKEN_COUNT,
};

/**
 * struct sof_topology_token - SOF topology token definition
 * @token:		Token number
 * @type:		Token type
 * @get_token:		Function pointer to parse the token value and save it in a object
 * @offset:		Offset within an object to save the token value into
 */
struct sof_topology_token {
	u32 token;
	u32 type;
	int (*get_token)(void *elem, void *object, u32 offset);
	u32 offset;
};

struct sof_token_info {
	const char *name;
	const struct sof_topology_token *tokens;
	int count;
};

/* PCM stream, mapped to FW component  */
struct snd_sof_pcm_stream {
	u32 comp_id;
	struct snd_dma_buffer page_table;
	struct sof_ipc_stream_posn posn;
	struct snd_pcm_substream *substream;
	struct snd_compr_stream *cstream;
	struct work_struct period_elapsed_work;
	struct snd_soc_dapm_widget_list *list; /* list of connected DAPM widgets */
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
	const char *name;
	int comp_id;
	int min_volume_step; /* min volume step for volume_table */
	int max_volume_step; /* max volume step for volume_table */
	int num_channels;
	unsigned int access;
	u32 readback_offset; /* offset to mmapped data if used */
	int info_type;
	int index; /* pipeline ID */
	void *priv; /* private data copied from topology */
	size_t priv_size; /* size of private data */
	size_t max_size;
	void *ipc_control_data;
	int max; /* applicable to volume controls */
	u32 size;	/* cdata size */
	u32 *volume_table; /* volume table computed from tlv data*/

	struct list_head list;	/* list in sdev control list */

	struct snd_sof_led_control led_ctl;

	/* if true, the control's data needs to be updated from Firmware */
	bool comp_data_dirty;
};

/** struct snd_sof_dai_link - DAI link info
 * @tuples: array of parsed tuples
 * @num_tuples: number of tuples in the tuples array
 * @link: Pointer to snd_soc_dai_link
 * @hw_configs: Pointer to hw configs in topology
 * @num_hw_configs: Number of hw configs in topology
 * @default_hw_cfg_id: Default hw config ID
 * @type: DAI type
 * @list: item in snd_sof_dev dai_link list
 */
struct snd_sof_dai_link {
	struct snd_sof_tuple *tuples;
	int num_tuples;
	struct snd_soc_dai_link *link;
	struct snd_soc_tplg_hw_config *hw_configs;
	int num_hw_configs;
	int default_hw_cfg_id;
	int type;
	struct list_head list;
};

/* ASoC SOF DAPM widget */
struct snd_sof_widget {
	struct snd_soc_component *scomp;
	int comp_id;
	int pipeline_id;
	int complete;
	int use_count; /* use_count will be protected by the PCM mutex held by the core */
	int core;
	int id;

	/*
	 * Flag indicating if the widget should be set up dynamically when a PCM is opened.
	 * This flag is only set for the scheduler type widget in topology. During topology
	 * loading, this flag is propagated to all the widgets belonging to the same pipeline.
	 * When this flag is not set, a widget is set up at the time of topology loading
	 * and retained until the DSP enters D3. It will need to be set up again when resuming
	 * from D3.
	 */
	bool dynamic_pipeline_widget;

	struct snd_soc_dapm_widget *widget;
	struct list_head list;	/* list in sdev widget list */
	struct snd_sof_widget *pipe_widget;

	const guid_t uuid;

	int num_tuples;
	struct snd_sof_tuple *tuples;

	void *private;		/* core does not touch this */
};

/* ASoC SOF DAPM route */
struct snd_sof_route {
	struct snd_soc_component *scomp;

	struct snd_soc_dapm_route *route;
	struct list_head list;	/* list in sdev route list */
	struct snd_sof_widget *src_widget;
	struct snd_sof_widget *sink_widget;
	bool setup;

	void *private;
};

/* ASoC DAI device */
struct snd_sof_dai {
	struct snd_soc_component *scomp;
	const char *name;

	int number_configs;
	int current_config;
	struct list_head list;	/* list in sdev dai list */
	void *private;
};

/*
 * Kcontrols.
 */

int snd_sof_volume_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol);
int snd_sof_volume_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol);
int snd_sof_volume_info(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo);
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
void snd_sof_control_notify(struct snd_sof_dev *sdev,
			    struct sof_ipc_ctrl_data *cdata);

/*
 * Topology.
 * There is no snd_sof_free_topology since topology components will
 * be freed by snd_soc_unregister_component,
 */
int snd_sof_load_topology(struct snd_soc_component *scomp, const char *file);

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
void snd_sof_pcm_period_elapsed(struct snd_pcm_substream *substream);
void snd_sof_pcm_init_elapsed_work(struct work_struct *work);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_COMPRESS)
void snd_sof_compr_fragment_elapsed(struct snd_compr_stream *cstream);
void snd_sof_compr_init_elapsed_work(struct work_struct *work);
#else
static inline void snd_sof_compr_fragment_elapsed(struct snd_compr_stream *cstream) { }
static inline void snd_sof_compr_init_elapsed_work(struct work_struct *work) { }
#endif

/*
 * Mixer IPC
 */
int snd_sof_ipc_set_get_comp_data(struct snd_sof_control *scontrol, bool set);

/* DAI link fixup */
int sof_pcm_dai_link_fixup(struct snd_soc_pcm_runtime *rtd, struct snd_pcm_hw_params *params);

/* PM */
int sof_set_up_pipelines(struct snd_sof_dev *sdev, bool verify);
int sof_tear_down_pipelines(struct snd_sof_dev *sdev, bool verify);
int sof_set_hw_params_upon_resume(struct device *dev);
bool snd_sof_stream_suspend_ignored(struct snd_sof_dev *sdev);
bool snd_sof_dsp_only_d0i3_compatible_stream_active(struct snd_sof_dev *sdev);

/* Machine driver enumeration */
int sof_machine_register(struct snd_sof_dev *sdev, void *pdata);
void sof_machine_unregister(struct snd_sof_dev *sdev, void *pdata);

int sof_widget_setup(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget);
int sof_widget_free(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget);

/* PCM */
int sof_widget_list_setup(struct snd_sof_dev *sdev, struct snd_sof_pcm *spcm, int dir);
int sof_widget_list_free(struct snd_sof_dev *sdev, struct snd_sof_pcm *spcm, int dir);
int sof_pcm_dsp_pcm_free(struct snd_pcm_substream *substream, struct snd_sof_dev *sdev,
			 struct snd_sof_pcm *spcm);
int sof_pcm_stream_free(struct snd_sof_dev *sdev, struct snd_pcm_substream *substream,
			struct snd_sof_pcm *spcm, int dir, bool free_widget_list);
int get_token_u32(void *elem, void *object, u32 offset);
int get_token_u16(void *elem, void *object, u32 offset);
int get_token_comp_format(void *elem, void *object, u32 offset);
int get_token_dai_type(void *elem, void *object, u32 offset);
int get_token_uuid(void *elem, void *object, u32 offset);
int sof_update_ipc_object(struct snd_soc_component *scomp, void *object, enum sof_tokens token_id,
			  struct snd_sof_tuple *tuples, int num_tuples,
			  size_t object_size, int token_instance_num);
#endif
