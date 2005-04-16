#ifndef __SOUND_WAVEFRONT_FX_H
#define __SOUND_WAVEFRONT_FX_H

extern int  snd_wavefront_fx_detect (snd_wavefront_t *);
extern void snd_wavefront_fx_ioctl  (snd_synth_t *sdev, 
				     unsigned int cmd, 
				     unsigned long arg);

#endif  __SOUND_WAVEFRONT_FX_H
