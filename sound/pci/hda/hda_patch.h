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

static const struct hda_codec_preset *hda_preset_tables[] = {
	snd_hda_preset_realtek,
	snd_hda_preset_cmedia,
	snd_hda_preset_analog,
	snd_hda_preset_sigmatel,
	snd_hda_preset_si3054,
	NULL
};
