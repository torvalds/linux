#ifndef __ASM_ARCH_AUDIO_H__
#define __ASM_ARCH_AUDIO_H__

#include <sound/driver.h>
#include <sound/core.h>
#include <sound/pcm.h>

typedef struct {
	int (*startup)(struct snd_pcm_substream *, void *);
	void (*shutdown)(struct snd_pcm_substream *, void *);
	void (*suspend)(void *);
	void (*resume)(void *);
	void *priv;
} pxa2xx_audio_ops_t;

#endif
