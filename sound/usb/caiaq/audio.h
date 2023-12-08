/* SPDX-License-Identifier: GPL-2.0 */
#ifndef CAIAQ_AUDIO_H
#define CAIAQ_AUDIO_H

int snd_usb_caiaq_audio_init(struct snd_usb_caiaqdev *cdev);
void snd_usb_caiaq_audio_free(struct snd_usb_caiaqdev *cdev);

#endif /* CAIAQ_AUDIO_H */
