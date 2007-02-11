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
	snd_hda_preset_realtek,
	snd_hda_preset_cmedia,
	snd_hda_preset_analog,
	snd_hda_preset_sigmatel,
	snd_hda_preset_si3054,
	snd_hda_preset_atihdmi,
	snd_hda_preset_conexant,
	snd_hda_preset_via,
	NULL
};
