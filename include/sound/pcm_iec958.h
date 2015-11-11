#ifndef __SOUND_PCM_IEC958_H
#define __SOUND_PCM_IEC958_H

#include <linux/types.h>

int snd_pcm_create_iec958_consumer(struct snd_pcm_runtime *runtime, u8 *cs,
	size_t len);

#endif
