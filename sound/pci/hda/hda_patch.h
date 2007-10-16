/*
 * HDA Patches - included by hda_codec.c
 */

/* Realtek codecs */
extern struct hda_codec_preset snd_hda_preset_realtek[];
/* C-Media codecs */
extern struct hda_codec_preset snd_hda_preset_cmedia[];
/* Analog Devices codecs */
extern struct hda_codec_preset snd_hda_preset_analog[];
/* SigmaTel codecs */
extern struct hda_codec_preset snd_hda_preset_sigmatel[];
/* SiLabs 3054/3055 modem codecs */
extern struct hda_codec_preset snd_hda_preset_si3054[];
/* ATI HDMI codecs */
extern struct hda_codec_preset snd_hda_preset_atihdmi[];
/* Conexant audio codec */
extern struct hda_codec_preset snd_hda_preset_conexant[];
/* VIA codecs */
extern struct hda_codec_preset snd_hda_preset_via[];

static const struct hda_codec_preset *hda_preset_tables[] = {
#ifdef CONFIG_SND_HDA_CODEC_REALTEK
	snd_hda_preset_realtek,
#endif
#ifdef CONFIG_SND_HDA_CODEC_CMEDIA
	snd_hda_preset_cmedia,
#endif
#ifdef CONFIG_SND_HDA_CODEC_ANALOG
	snd_hda_preset_analog,
#endif
#ifdef CONFIG_SND_HDA_CODEC_SIGMATEL
	snd_hda_preset_sigmatel,
#endif
#ifdef CONFIG_SND_HDA_CODEC_SI3054
	snd_hda_preset_si3054,
#endif
#ifdef CONFIG_SND_HDA_CODEC_ATIHDMI
	snd_hda_preset_atihdmi,
#endif
#ifdef CONFIG_SND_HDA_CODEC_CONEXANT
	snd_hda_preset_conexant,
#endif
#ifdef CONFIG_SND_HDA_CODEC_VIA
	snd_hda_preset_via,
#endif
	NULL
};
