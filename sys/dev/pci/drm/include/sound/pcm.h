/* Public domain. */

#ifndef _SOUND_PCM_H
#define _SOUND_PCM_H

#define SNDRV_CHMAP_UNKNOWN	0
#define SNDRV_CHMAP_FL		1
#define SNDRV_CHMAP_FR		2
#define SNDRV_CHMAP_RL		3
#define SNDRV_CHMAP_RR		4
#define SNDRV_CHMAP_FC		5
#define SNDRV_CHMAP_LFE		6
#define SNDRV_CHMAP_RC		7
#define SNDRV_CHMAP_FLC		8
#define SNDRV_CHMAP_FRC		9
#define SNDRV_CHMAP_RLC		10
#define SNDRV_CHMAP_RRC		11
#define SNDRV_CHMAP_FLW		12
#define SNDRV_CHMAP_FRW		13
#define SNDRV_CHMAP_FLH		14
#define SNDRV_CHMAP_FCH		15
#define SNDRV_CHMAP_FRH		16
#define SNDRV_CHMAP_TC		17

#define SNDRV_PCM_RATE_KNOT	-1

#define SNDRV_PCM_FMTBIT_S16	0x0001
#define SNDRV_PCM_FMTBIT_S20	0x0002
#define SNDRV_PCM_FMTBIT_S24	0x0004
#define SNDRV_PCM_FMTBIT_S32	0x0008

struct snd_pcm_chmap_elem {
	u_char channels;
	u_char map[15];
};

static inline int
snd_pcm_rate_to_rate_bit(u_int rate)
{
	return SNDRV_PCM_RATE_KNOT;
}

#endif
