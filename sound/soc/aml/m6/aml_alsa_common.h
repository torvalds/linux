#ifndef __AML_ALSA_COMMOM_H__
#define __AML_ALSA_COMMON_H__

#define VOLUME_SCALE	100
#define VOLUME_SHIFT	15

extern int aml_alsa_create_ctrl(struct snd_card *card, void *p_value);

extern int get_mixer_output_volume(void);

extern int set_mixer_output_volume(int volume);
#endif
