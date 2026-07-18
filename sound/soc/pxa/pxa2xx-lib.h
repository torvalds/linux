/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PXA2XX_LIB_H
#define __PXA2XX_LIB_H

#include <uapi/sound/asound.h>
#include <linux/platform_device.h>

/* PCM */
struct snd_pcm_substream;
struct snd_pcm_hw_params;
struct snd_soc_pcm_runtime;
struct snd_pcm;
struct snd_soc_component;

int pxa2xx_soc_pcm_new(struct snd_soc_component *component,
		       struct snd_soc_pcm_runtime *rtd);
int pxa2xx_soc_pcm_open(struct snd_soc_component *component,
		        struct snd_pcm_substream *substream);
int pxa2xx_soc_pcm_close(struct snd_soc_component *component,
			 struct snd_pcm_substream *substream);
int pxa2xx_soc_pcm_hw_params(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params);
int pxa2xx_soc_pcm_prepare(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream);
int pxa2xx_soc_pcm_trigger(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream, int cmd);
snd_pcm_uframes_t pxa2xx_soc_pcm_pointer(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream);

/* AC97 */
int pxa2xx_ac97_read(int slot, unsigned short reg);
int pxa2xx_ac97_write(int slot, unsigned short reg, unsigned short val);

bool pxa2xx_ac97_try_warm_reset(void);
bool pxa2xx_ac97_try_cold_reset(void);
void pxa2xx_ac97_finish_reset(void);

int pxa2xx_ac97_hw_suspend(void);
int pxa2xx_ac97_hw_resume(void);

int pxa2xx_ac97_hw_probe(struct platform_device *dev);
void pxa2xx_ac97_hw_remove(struct platform_device *dev);

#endif
