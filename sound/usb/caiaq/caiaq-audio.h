#ifndef CAIAQ_AUDIO_H
#define CAIAQ_AUDIO_H

int snd_usb_caiaq_audio_init(struct snd_usb_caiaqdev *dev);
void snd_usb_caiaq_audio_free(struct snd_usb_caiaqdev *dev);

#endif /* CAIAQ_AUDIO_H */
