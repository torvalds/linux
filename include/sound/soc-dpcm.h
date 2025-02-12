/* SPDX-License-Identifier: GPL-2.0
 *
 * linux/sound/soc-dpcm.h -- ALSA SoC Dynamic PCM Support
 *
 * Author:		Liam Girdwood <lrg@ti.com>
 */

#ifndef __LINUX_SND_SOC_DPCM_H
#define __LINUX_SND_SOC_DPCM_H

#include <linux/slab.h>
#include <linux/list.h>
#include <sound/pcm.h>

struct snd_soc_pcm_runtime;

/*
 * Types of runtime_update to perform. e.g. originated from FE PCM ops
 * or audio route changes triggered by muxes/mixers.
 */
enum snd_soc_dpcm_update {
	SND_SOC_DPCM_UPDATE_NO	= 0,
	SND_SOC_DPCM_UPDATE_BE,
	SND_SOC_DPCM_UPDATE_FE,
};

/*
 * Dynamic PCM Frontend -> Backend link management states.
 */
enum snd_soc_dpcm_link_state {
	SND_SOC_DPCM_LINK_STATE_NEW	= 0,	/* newly created link */
	SND_SOC_DPCM_LINK_STATE_FREE,		/* link to be dismantled */
};

/*
 * Dynamic PCM Frontend -> Backend link PCM states.
 */
enum snd_soc_dpcm_state {
	SND_SOC_DPCM_STATE_NEW	= 0,
	SND_SOC_DPCM_STATE_OPEN,
	SND_SOC_DPCM_STATE_HW_PARAMS,
	SND_SOC_DPCM_STATE_PREPARE,
	SND_SOC_DPCM_STATE_START,
	SND_SOC_DPCM_STATE_STOP,
	SND_SOC_DPCM_STATE_PAUSED,
	SND_SOC_DPCM_STATE_SUSPEND,
	SND_SOC_DPCM_STATE_HW_FREE,
	SND_SOC_DPCM_STATE_CLOSE,
};

/*
 * Dynamic PCM trigger ordering. Triggering flexibility is required as some
 * DSPs require triggering before/after their CPU platform and DAIs.
 *
 * i.e. some clients may want to manually order this call in their PCM
 * trigger() whilst others will just use the regular core ordering.
 */
enum snd_soc_dpcm_trigger {
	SND_SOC_DPCM_TRIGGER_PRE		= 0,
	SND_SOC_DPCM_TRIGGER_POST,
};

/*
 * Dynamic PCM link
 * This links together a FE and BE DAI at runtime and stores the link
 * state information and the hw_params configuration.
 */
struct snd_soc_dpcm {
	/* FE and BE DAIs*/
	struct snd_soc_pcm_runtime *be;
	struct snd_soc_pcm_runtime *fe;

	/* link state */
	enum snd_soc_dpcm_link_state state;

	/* list of BE and FE for this DPCM link */
	struct list_head list_be;
	struct list_head list_fe;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_state;
#endif
};

/*
 * Dynamic PCM runtime data.
 */
struct snd_soc_dpcm_runtime {
	struct list_head be_clients;
	struct list_head fe_clients;

	int users;
	struct snd_pcm_hw_params hw_params;

	/* state and update */
	enum snd_soc_dpcm_update runtime_update;
	enum snd_soc_dpcm_state state;

	int trigger_pending; /* trigger cmd + 1 if pending, 0 if not */

	int be_start; /* refcount protected by BE stream pcm lock */
	int be_pause; /* refcount protected by BE stream pcm lock */
	bool fe_pause; /* used to track STOP after PAUSE */
};

#define for_each_dpcm_fe(be, stream, _dpcm)				\
	list_for_each_entry(_dpcm, &(be)->dpcm[stream].fe_clients, list_fe)

#define for_each_dpcm_be(fe, stream, _dpcm)				\
	list_for_each_entry(_dpcm, &(fe)->dpcm[stream].be_clients, list_be)
#define for_each_dpcm_be_safe(fe, stream, _dpcm, __dpcm)			\
	list_for_each_entry_safe(_dpcm, __dpcm, &(fe)->dpcm[stream].be_clients, list_be)
#define for_each_dpcm_be_rollback(fe, stream, _dpcm)			\
	list_for_each_entry_continue_reverse(_dpcm, &(fe)->dpcm[stream].be_clients, list_be)


/* get the substream for this BE */
struct snd_pcm_substream *
	snd_soc_dpcm_get_substream(struct snd_soc_pcm_runtime *be, int stream);

/* update audio routing between PCMs and any DAI links */
int snd_soc_dpcm_runtime_update(struct snd_soc_card *card);

#ifdef CONFIG_DEBUG_FS
void soc_dpcm_debugfs_add(struct snd_soc_pcm_runtime *rtd);
#else
static inline void soc_dpcm_debugfs_add(struct snd_soc_pcm_runtime *rtd)
{
}
#endif

int dpcm_path_get(struct snd_soc_pcm_runtime *fe,
	int stream, struct snd_soc_dapm_widget_list **list_);
void dpcm_path_put(struct snd_soc_dapm_widget_list **list);
int dpcm_add_paths(struct snd_soc_pcm_runtime *fe, int stream,
		   struct snd_soc_dapm_widget_list **list_);
int dpcm_be_dai_startup(struct snd_soc_pcm_runtime *fe, int stream);
void dpcm_be_dai_stop(struct snd_soc_pcm_runtime *fe, int stream,
		      int do_hw_free, struct snd_soc_dpcm *last);
void dpcm_be_disconnect(struct snd_soc_pcm_runtime *fe, int stream);
void dpcm_clear_pending_state(struct snd_soc_pcm_runtime *fe, int stream);
void dpcm_be_dai_hw_free(struct snd_soc_pcm_runtime *fe, int stream);
int dpcm_be_dai_hw_params(struct snd_soc_pcm_runtime *fe, int tream);
int dpcm_be_dai_trigger(struct snd_soc_pcm_runtime *fe, int stream, int cmd);
int dpcm_be_dai_prepare(struct snd_soc_pcm_runtime *fe, int stream);
void dpcm_dapm_stream_event(struct snd_soc_pcm_runtime *fe, int dir, int event);

bool dpcm_end_walk_at_be(struct snd_soc_dapm_widget *widget, enum snd_soc_dapm_direction dir);
int widget_in_list(struct snd_soc_dapm_widget_list *list,
		   struct snd_soc_dapm_widget *widget);

#define dpcm_be_dai_startup_rollback(fe, stream, last)	\
						dpcm_be_dai_stop(fe, stream, 0, last)
#define dpcm_be_dai_startup_unwind(fe, stream)	dpcm_be_dai_stop(fe, stream, 0, NULL)
#define dpcm_be_dai_shutdown(fe, stream)	dpcm_be_dai_stop(fe, stream, 1, NULL)

#endif
