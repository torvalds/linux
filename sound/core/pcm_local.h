/*
 * pcm_local.h - a local header file for snd-pcm module.
 *
 * Copyright (c) Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
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

int snd_pcm_hw_constraints_init(struct snd_pcm_substream *substream);
int snd_pcm_hw_constraints_complete(struct snd_pcm_substream *substream);

int snd_pcm_hw_params_choose(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params);

int snd_pcm_hw_constraint_mask(struct snd_pcm_runtime *runtime,
			       snd_pcm_hw_param_t var, u_int32_t mask);

int snd_pcm_update_state(struct snd_pcm_substream *substream,
			 struct snd_pcm_runtime *runtime);
int snd_pcm_update_hw_ptr(struct snd_pcm_substream *substream);

void snd_pcm_playback_silence(struct snd_pcm_substream *substream,
			      snd_pcm_uframes_t new_hw_ptr);

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

#endif	/* __SOUND_CORE_PCM_LOCAL_H */
