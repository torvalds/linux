#ifndef __HDAC_HDMI_H__
#define __HDAC_HDMI_H__

int hdac_hdmi_jack_init(struct snd_soc_dai *dai, int pcm,
				struct snd_soc_jack *jack);

#endif /* __HDAC_HDMI_H__ */
