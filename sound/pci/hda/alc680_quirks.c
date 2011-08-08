/*
 * ALC680 quirk models
 * included by patch_realtek.c
 */

/* ALC680 models */
enum {
	ALC680_AUTO,
	ALC680_BASE,
	ALC680_MODEL_LAST,
};

#define ALC680_DIGIN_NID	ALC880_DIGIN_NID
#define ALC680_DIGOUT_NID	ALC880_DIGOUT_NID
#define alc680_modes		alc260_modes

static const hda_nid_t alc680_dac_nids[3] = {
	/* Lout1, Lout2, hp */
	0x02, 0x03, 0x04
};

static const hda_nid_t alc680_adc_nids[3] = {
	/* ADC0-2 */
	/* DMIC, MIC, Line-in*/
	0x07, 0x08, 0x09
};

/*
 * Analog capture ADC cgange
 */
static hda_nid_t alc680_get_cur_adc(struct hda_codec *codec)
{
	static hda_nid_t pins[] = {0x18, 0x19};
	static hda_nid_t adcs[] = {0x08, 0x09};
	int i;

	for (i = 0; i < ARRAY_SIZE(pins); i++) {
		if (!is_jack_detectable(codec, pins[i]))
			continue;
		if (snd_hda_jack_detect(codec, pins[i]))
			return adcs[i];
	}
	return 0x07;
}

static void alc680_rec_autoswitch(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t nid = alc680_get_cur_adc(codec);
	if (spec->cur_adc && nid != spec->cur_adc) {
		__snd_hda_codec_cleanup_stream(codec, spec->cur_adc, 1);
		spec->cur_adc = nid;
		snd_hda_codec_setup_stream(codec, nid,
					   spec->cur_adc_stream_tag, 0,
					   spec->cur_adc_format);
	}
}

static int alc680_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      unsigned int stream_tag,
				      unsigned int format,
				      struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t nid = alc680_get_cur_adc(codec);

	spec->cur_adc = nid;
	spec->cur_adc_stream_tag = stream_tag;
	spec->cur_adc_format = format;
	snd_hda_codec_setup_stream(codec, nid, stream_tag, 0, format);
	return 0;
}

static int alc680_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	snd_hda_codec_cleanup_stream(codec, spec->cur_adc);
	spec->cur_adc = 0;
	return 0;
}

static const struct hda_pcm_stream alc680_pcm_analog_auto_capture = {
	.substreams = 1, /* can be overridden */
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
	.ops = {
		.prepare = alc680_capture_pcm_prepare,
		.cleanup = alc680_capture_pcm_cleanup
	},
};

static const struct snd_kcontrol_new alc680_base_mixer[] = {
	/* output mixer control */
	HDA_CODEC_VOLUME("Front Playback Volume", 0x2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x4, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x16, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x12, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line In Boost Volume", 0x19, 0, HDA_INPUT),
	{ }
};

static const struct hda_bind_ctls alc680_bind_cap_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x07, 3, 0, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x08, 3, 0, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x09, 3, 0, HDA_INPUT),
		0
	},
};

static const struct hda_bind_ctls alc680_bind_cap_switch = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x07, 3, 0, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x08, 3, 0, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x09, 3, 0, HDA_INPUT),
		0
	},
};

static const struct snd_kcontrol_new alc680_master_capture_mixer[] = {
	HDA_BIND_VOL("Capture Volume", &alc680_bind_cap_vol),
	HDA_BIND_SW("Capture Switch", &alc680_bind_cap_switch),
	{ } /* end */
};

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static const struct hda_verb alc680_init_verbs[] = {
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{0x16, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT   | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_MIC_EVENT  | AC_USRSP_EN},
	{0x19, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_MIC_EVENT  | AC_USRSP_EN},

	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc680_base_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x16;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x15;
	spec->autocfg.num_inputs = 2;
	spec->autocfg.inputs[0].pin = 0x18;
	spec->autocfg.inputs[0].type = AUTO_PIN_MIC;
	spec->autocfg.inputs[1].pin = 0x19;
	spec->autocfg.inputs[1].type = AUTO_PIN_LINE_IN;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc680_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	if ((res >> 26) == ALC_HP_EVENT)
		alc_hp_automute(codec);
	if ((res >> 26) == ALC_MIC_EVENT)
		alc680_rec_autoswitch(codec);
}

static void alc680_inithook(struct hda_codec *codec)
{
	alc_hp_automute(codec);
	alc680_rec_autoswitch(codec);
}

/*
 * configuration and preset
 */
static const char * const alc680_models[ALC680_MODEL_LAST] = {
	[ALC680_BASE]		= "base",
	[ALC680_AUTO]		= "auto",
};

static const struct snd_pci_quirk alc680_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1043, 0x12f3, "ASUS NX90", ALC680_BASE),
	{}
};

static const struct alc_config_preset alc680_presets[] = {
	[ALC680_BASE] = {
		.mixers = { alc680_base_mixer },
		.cap_mixer =  alc680_master_capture_mixer,
		.init_verbs = { alc680_init_verbs },
		.num_dacs = ARRAY_SIZE(alc680_dac_nids),
		.dac_nids = alc680_dac_nids,
		.dig_out_nid = ALC680_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc680_modes),
		.channel_mode = alc680_modes,
		.unsol_event = alc680_unsol_event,
		.setup = alc680_base_setup,
		.init_hook = alc680_inithook,

	},
};
