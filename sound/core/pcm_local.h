/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * pcm_local.h - a local header file for snd-pcm module.
 *
 * Copyright (c) Takashi Sakamoto <o-takashi@sakamocchi.jp>
 */

#ifndef __SOUND_CORE_PCM_LOCAL_H
#define __SOUND_CORE_PCM_LOCAL_H

extern const struct snd_pcm_hw_constraint_list snd_pcm_known_rates;

void snd_interval_mul(const struct snd_interval *a,
		      const struct snd_interval *b, struct snd_interval *c);
void snd_interval_div(const struct snd_interval *a,
		      const struct snd_interval *b, struct snd_interval *c);
void snd_interval_muldivk(const struct snd_interval *a,
			  const struct snd_interval *b,
			  unsigned int k, struct snd_interval *c);
void snd_interval_mulkdiv(const struct snd_interval *a, unsigned int k,
			  const struct snd_interval *b, struct snd_interval *c);

int snd_pcm_hw_constraint_mask(struct snd_pcm_runtime *runtime,
			       snd_pcm_hw_param_t var, u_int32_t mask);

int pcm_lib_apply_appl_ptr(struct snd_pcm_substream *substream,
			   snd_pcm_uframes_t appl_ptr);
int snd_pcm_update_state(struct snd_pcm_substream *substream,
			 struct snd_pcm_runtime *runtime);
int snd_pcm_update_hw_ptr(struct snd_pcm_substream *substream);

void snd_pcm_playback_silence(struct snd_pcm_substream *substream,
			      snd_pcm_uframes_t new_hw_ptr);

static inline snd_pcm_uframes_t
snd_pcm_avail(struct snd_pcm_substream *substream)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return snd_pcm_playback_avail(substream->runtime);
	else
		return snd_pcm_capture_avail(substream->runtime);
}

static inline snd_pcm_uframes_t
snd_pcm_hw_avail(struct snd_pcm_substream *substream)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return snd_pcm_playback_hw_avail(substream->runtime);
	else
		return snd_pcm_capture_hw_avail(substream->runtime);
}

#ifdef CONFIG_SND_PCM_TIMER
void snd_pcm_timer_resolution_change(struct snd_pcm_substream *substream);
void snd_pcm_timer_init(struct snd_pcm_substream *substream);
void snd_pcm_timer_done(struct snd_pcm_substream *substream);
#else
static inline void
snd_pcm_timer_resolution_change(struct snd_pcm_substream *substream) {}
static inline void snd_pcm_timer_init(struct snd_pcm_substream *substream) {}
static inline void snd_pcm_timer_done(struct snd_pcm_substream *substream) {}
#endif

void __snd_pcm_xrun(struct snd_pcm_substream *substream);
void snd_pcm_group_init(struct snd_pcm_group *group);
void snd_pcm_sync_stop(struct snd_pcm_substream *substream, bool sync_irq);

#define PCM_RUNTIME_CHECK(sub) snd_BUG_ON(!(sub) || !(sub)->runtime)

/* loop over all PCM substreams */
#define for_each_pcm_substream(pcm, str, subs) \
	for ((str) = 0; (str) < 2; (str)++) \
		for ((subs) = (pcm)->streams[str].substream; (subs); \
		     (subs) = (subs)->next)

static inline void snd_pcm_dma_buffer_sync(struct snd_pcm_substream *substream,
					   enum snd_dma_sync_mode mode)
{
	if (substream->runtime->info & SNDRV_PCM_INFO_EXPLICIT_SYNC)
		snd_dma_buffer_sync(snd_pcm_get_dma_buf(substream), mode);
}

#endif	/* __SOUND_CORE_PCM_LOCAL_H */
