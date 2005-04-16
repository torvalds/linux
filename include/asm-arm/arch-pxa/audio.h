#ifndef __ASM_ARCH_AUDIO_H__
#define __ASM_ARCH_AUDIO_H__

#include <sound/driver.h>
#include <sound/core.h>
#include <sound/pcm.h>

typedef struct {
	int (*startup)(snd_pcm_substream_t *, void *);
	void (*shutdown)(snd_pcm_substream_t *, void *);
	void (*suspend)(void *);
	void (*resume)(void *);
	void *priv;
} pxa2xx_audio_ops_t;

#endif
