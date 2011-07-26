/*
 * ALC882/ALC883/ALC888/ALC889 quirk models
 * included by patch_realtek.c
 */

/* ALC882 models */
enum {
	ALC882_AUTO,
	ALC882_3ST_DIG,
	ALC882_6ST_DIG,
	ALC882_ARIMA,
	ALC882_W2JC,
	ALC882_TARGA,
	ALC882_ASUS_A7J,
	ALC882_ASUS_A7M,
	ALC885_MACPRO,
	ALC885_MBA21,
	ALC885_MBP3,
	ALC885_MB5,
	ALC885_MACMINI3,
	ALC885_IMAC24,
	ALC885_IMAC91,
	ALC883_3ST_2ch_DIG,
	ALC883_3ST_6ch_DIG,
	ALC883_3ST_6ch,
	ALC883_6ST_DIG,
	ALC883_TARGA_DIG,
	ALC883_TARGA_2ch_DIG,
	ALC883_TARGA_8ch_DIG,
	ALC883_ACER,
	ALC883_ACER_ASPIRE,
	ALC888_ACER_ASPIRE_4930G,
	ALC888_ACER_ASPIRE_6530G,
	ALC888_ACER_ASPIRE_8930G,
	ALC888_ACER_ASPIRE_7730G,
	ALC883_MEDION,
	ALC883_MEDION_WIM2160,
	ALC883_LAPTOP_EAPD,
	ALC883_LENOVO_101E_2ch,
	ALC883_LENOVO_NB0763,
	ALC888_LENOVO_MS7195_DIG,
	ALC888_LENOVO_SKY,
	ALC883_HAIER_W66,
	ALC888_3ST_HP,
	ALC888_6ST_DELL,
	ALC883_MITAC,
	ALC883_CLEVO_M540R,
	ALC883_CLEVO_M720,
	ALC883_FUJITSU_PI2515,
	ALC888_FUJITSU_XA3530,
	ALC883_3ST_6ch_INTEL,
	ALC889A_INTEL,
	ALC889_INTEL,
	ALC888_ASUS_M90V,
	ALC888_ASUS_EEE1601,
	ALC889A_MB31,
	ALC1200_ASUS_P5Q,
	ALC883_SONY_VAIO_TT,
	ALC882_MODEL_LAST,
};

/*
 * 2ch mode
 */
static const struct hda_verb alc888_4ST_ch2_intel_init[] = {
/* Mic-in jack as mic in */
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
/* Line-in jack as Line in */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
/* Line-Out as Front */
	{ 0x17, AC_VERB_SET_CONNECT_SEL, 0x00},
	{ } /* end */
};

/*
 * 4ch mode
 */
static const struct hda_verb alc888_4ST_ch4_intel_init[] = {
/* Mic-in jack as mic in */
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
/* Line-in jack as Surround */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
/* Line-Out as Front */
	{ 0x17, AC_VERB_SET_CONNECT_SEL, 0x00},
	{ } /* end */
};

/*
 * 6ch mode
 */
static const struct hda_verb alc888_4ST_ch6_intel_init[] = {
/* Mic-in jack as CLFE */
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
/* Line-in jack as Surround */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
/* Line-Out as CLFE (workaround because Mic-in is not loud enough) */
	{ 0x17, AC_VERB_SET_CONNECT_SEL, 0x03},
	{ } /* end */
};

/*
 * 8ch mode
 */
static const struct hda_verb alc888_4ST_ch8_intel_init[] = {
/* Mic-in jack as CLFE */
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
/* Line-in jack as Surround */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
/* Line-Out as Side */
	{ 0x17, AC_VERB_SET_CONNECT_SEL, 0x03},
	{ } /* end */
};

static const struct hda_channel_mode alc888_4ST_8ch_intel_modes[4] = {
	{ 2, alc888_4ST_ch2_intel_init },
	{ 4, alc888_4ST_ch4_intel_init },
	{ 6, alc888_4ST_ch6_intel_init },
	{ 8, alc888_4ST_ch8_intel_init },
};

/*
 * ALC888 Fujitsu Siemens Amillo xa3530
 */

static const struct hda_verb alc888_fujitsu_xa3530_verbs[] = {
/* Front Mic: set to PIN_IN (empty by default) */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
/* Connect Internal HP to Front */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
/* Connect Bass HP to Front */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
/* Connect Line-Out side jack (SPDIF) to Side */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x03},
/* Connect Mic jack to CLFE */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02},
/* Connect Line-in jack to Surround */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01},
/* Connect HP out jack to Front */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
/* Enable unsolicited event for HP jack and Line-out jack */
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
	{0x17, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
	{}
};

static void alc889_automute_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x16;
	spec->autocfg.speaker_pins[2] = 0x17;
	spec->autocfg.speaker_pins[3] = 0x19;
	spec->autocfg.speaker_pins[4] = 0x1a;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc889_intel_init_hook(struct hda_codec *codec)
{
	alc889_coef_init(codec);
	alc_hp_automute(codec);
}

static void alc888_fujitsu_xa3530_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x17; /* line-out */
	spec->autocfg.hp_pins[1] = 0x1b; /* hp */
	spec->autocfg.speaker_pins[0] = 0x14; /* speaker */
	spec->autocfg.speaker_pins[1] = 0x15; /* bass */
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

/*
 * ALC888 Acer Aspire 4930G model
 */

static const struct hda_verb alc888_acer_aspire_4930g_verbs[] = {
/* Front Mic: set to PIN_IN (empty by default) */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
/* Unselect Front Mic by default in input mixer 3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0xb)},
/* Enable unsolicited event for HP jack */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
/* Connect Internal HP to front */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
/* Connect HP out to front */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

/*
 * ALC888 Acer Aspire 6530G model
 */

static const struct hda_verb alc888_acer_aspire_6530g_verbs[] = {
/* Route to built-in subwoofer as well as speakers */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
/* Bias voltage on for external mic port */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN | PIN_VREF80},
/* Front Mic: set to PIN_IN (empty by default) */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
/* Unselect Front Mic by default in input mixer 3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0xb)},
/* Enable unsolicited event for HP jack */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
/* Enable speaker output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
/* Enable headphone output */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

/*
 *ALC888 Acer Aspire 7730G model
 */

static const struct hda_verb alc888_acer_aspire_7730G_verbs[] = {
/* Bias voltage on for external mic port */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN | PIN_VREF80},
/* Front Mic: set to PIN_IN (empty by default) */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
/* Unselect Front Mic by default in input mixer 3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0xb)},
/* Enable unsolicited event for HP jack */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
/* Enable speaker output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
/* Enable headphone output */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
/*Enable internal subwoofer */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x02},
	{0x17, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

/*
 * ALC889 Acer Aspire 8930G model
 */

static const struct hda_verb alc889_acer_aspire_8930g_verbs[] = {
/* Front Mic: set to PIN_IN (empty by default) */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
/* Unselect Front Mic by default in input mixer 3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0xb)},
/* Enable unsolicited event for HP jack */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
/* Connect Internal Front to Front */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
/* Connect Internal Rear to Rear */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x01},
/* Connect Internal CLFE to CLFE */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x02},
/* Connect HP out to Front */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
/* Enable all DACs */
/*  DAC DISABLE/MUTE 1? */
/*  setting bits 1-5 disables DAC nids 0x02-0x06 apparently. Init=0x38 */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x03},
	{0x20, AC_VERB_SET_PROC_COEF, 0x0000},
/*  DAC DISABLE/MUTE 2? */
/*  some bit here disables the other DACs. Init=0x4900 */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x08},
	{0x20, AC_VERB_SET_PROC_COEF, 0x0000},
/* DMIC fix
 * This laptop has a stereo digital microphone. The mics are only 1cm apart
 * which makes the stereo useless. However, either the mic or the ALC889
 * makes the signal become a difference/sum signal instead of standard
 * stereo, which is annoying. So instead we flip this bit which makes the
 * codec replicate the sum signal to both channels, turning it into a
 * normal mono mic.
 */
/*  DMIC_CONTROL? Init value = 0x0001 */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x0b},
	{0x20, AC_VERB_SET_PROC_COEF, 0x0003},
	{ }
};

static const struct hda_input_mux alc888_2_capture_sources[2] = {
	/* Front mic only available on one ADC */
	{
		.num_items = 4,
		.items = {
			{ "Mic", 0x0 },
			{ "Line", 0x2 },
			{ "CD", 0x4 },
			{ "Front Mic", 0xb },
		},
	},
	{
		.num_items = 3,
		.items = {
			{ "Mic", 0x0 },
			{ "Line", 0x2 },
			{ "CD", 0x4 },
		},
	}
};

static const struct hda_input_mux alc888_acer_aspire_6530_sources[2] = {
	/* Interal mic only available on one ADC */
	{
		.num_items = 5,
		.items = {
			{ "Mic", 0x0 },
			{ "Line In", 0x2 },
			{ "CD", 0x4 },
			{ "Input Mix", 0xa },
			{ "Internal Mic", 0xb },
		},
	},
	{
		.num_items = 4,
		.items = {
			{ "Mic", 0x0 },
			{ "Line In", 0x2 },
			{ "CD", 0x4 },
			{ "Input Mix", 0xa },
		},
	}
};

static const struct hda_input_mux alc889_capture_sources[3] = {
	/* Digital mic only available on first "ADC" */
	{
		.num_items = 5,
		.items = {
			{ "Mic", 0x0 },
			{ "Line", 0x2 },
			{ "CD", 0x4 },
			{ "Front Mic", 0xb },
			{ "Input Mix", 0xa },
		},
	},
	{
		.num_items = 4,
		.items = {
			{ "Mic", 0x0 },
			{ "Line", 0x2 },
			{ "CD", 0x4 },
			{ "Input Mix", 0xa },
		},
	},
	{
		.num_items = 4,
		.items = {
			{ "Mic", 0x0 },
			{ "Line", 0x2 },
			{ "CD", 0x4 },
			{ "Input Mix", 0xa },
		},
	}
};

static const struct snd_kcontrol_new alc888_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0,
		HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc888_acer_aspire_4930g_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0,
		HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Internal LFE Playback Volume", 0x0f, 1, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Internal LFE Playback Switch", 0x0f, 1, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc889_acer_aspire_8930g_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0,
		HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};


static void alc888_acer_aspire_4930g_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x16;
	spec->autocfg.speaker_pins[2] = 0x17;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc888_acer_aspire_6530g_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x16;
	spec->autocfg.speaker_pins[2] = 0x17;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc888_acer_aspire_7730g_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x16;
	spec->autocfg.speaker_pins[2] = 0x17;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc889_acer_aspire_8930g_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x16;
	spec->autocfg.speaker_pins[2] = 0x1b;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

#define ALC882_DIGOUT_NID	0x06
#define ALC882_DIGIN_NID	0x0a
#define ALC883_DIGOUT_NID	ALC882_DIGOUT_NID
#define ALC883_DIGIN_NID	ALC882_DIGIN_NID
#define ALC1200_DIGOUT_NID	0x10


static const struct hda_channel_mode alc882_ch_modes[1] = {
	{ 8, NULL }
};

/* DACs */
static const hda_nid_t alc882_dac_nids[4] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x03, 0x04, 0x05
};
#define alc883_dac_nids		alc882_dac_nids

/* ADCs */
#define alc882_adc_nids		alc880_adc_nids
#define alc882_adc_nids_alt	alc880_adc_nids_alt
#define alc883_adc_nids		alc882_adc_nids_alt
static const hda_nid_t alc883_adc_nids_alt[1] = { 0x08 };
static const hda_nid_t alc883_adc_nids_rev[2] = { 0x09, 0x08 };
#define alc889_adc_nids		alc880_adc_nids

static const hda_nid_t alc882_capsrc_nids[3] = { 0x24, 0x23, 0x22 };
static const hda_nid_t alc882_capsrc_nids_alt[2] = { 0x23, 0x22 };
#define alc883_capsrc_nids	alc882_capsrc_nids_alt
static const hda_nid_t alc883_capsrc_nids_rev[2] = { 0x22, 0x23 };
#define alc889_capsrc_nids	alc882_capsrc_nids

/* input MUX */
/* FIXME: should be a matrix-type input source selection */

static const struct hda_input_mux alc882_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

#define alc883_capture_source	alc882_capture_source

static const struct hda_input_mux alc889_capture_source = {
	.num_items = 3,
	.items = {
		{ "Front Mic", 0x0 },
		{ "Mic", 0x3 },
		{ "Line", 0x2 },
	},
};

static const struct hda_input_mux mb5_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x1 },
		{ "Line", 0x7 },
		{ "CD", 0x4 },
	},
};

static const struct hda_input_mux macmini3_capture_source = {
	.num_items = 2,
	.items = {
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static const struct hda_input_mux alc883_3stack_6ch_intel = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x1 },
		{ "Front Mic", 0x0 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static const struct hda_input_mux alc883_lenovo_101e_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x1 },
		{ "Line", 0x2 },
	},
};

static const struct hda_input_mux alc883_lenovo_nb0763_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static const struct hda_input_mux alc883_fujitsu_pi2515_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x1 },
	},
};

static const struct hda_input_mux alc883_lenovo_sky_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x4 },
	},
};

static const struct hda_input_mux alc883_asus_eee1601_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x0 },
		{ "Line", 0x2 },
	},
};

static const struct hda_input_mux alc889A_mb31_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x0 },
		/* Front Mic (0x01) unused */
		{ "Line", 0x2 },
		/* Line 2 (0x03) unused */
		/* CD (0x04) unused? */
	},
};

static const struct hda_input_mux alc889A_imac91_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x01 },
		{ "Line", 0x2 }, /* Not sure! */
	},
};

/*
 * 2ch mode
 */
static const struct hda_channel_mode alc883_3ST_2ch_modes[1] = {
	{ 2, NULL }
};

/*
 * 2ch mode
 */
static const struct hda_verb alc882_3ST_ch2_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 4ch mode
 */
static const struct hda_verb alc882_3ST_ch4_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

/*
 * 6ch mode
 */
static const struct hda_verb alc882_3ST_ch6_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static const struct hda_channel_mode alc882_3ST_6ch_modes[3] = {
	{ 2, alc882_3ST_ch2_init },
	{ 4, alc882_3ST_ch4_init },
	{ 6, alc882_3ST_ch6_init },
};

#define alc883_3ST_6ch_modes	alc882_3ST_6ch_modes

/*
 * 2ch mode
 */
static const struct hda_verb alc883_3ST_ch2_clevo_init[] = {
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 4ch mode
 */
static const struct hda_verb alc883_3ST_ch4_clevo_init[] = {
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

/*
 * 6ch mode
 */
static const struct hda_verb alc883_3ST_ch6_clevo_init[] = {
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static const struct hda_channel_mode alc883_3ST_6ch_clevo_modes[3] = {
	{ 2, alc883_3ST_ch2_clevo_init },
	{ 4, alc883_3ST_ch4_clevo_init },
	{ 6, alc883_3ST_ch6_clevo_init },
};


/*
 * 6ch mode
 */
static const struct hda_verb alc882_sixstack_ch6_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

/*
 * 8ch mode
 */
static const struct hda_verb alc882_sixstack_ch8_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

static const struct hda_channel_mode alc882_sixstack_modes[2] = {
	{ 6, alc882_sixstack_ch6_init },
	{ 8, alc882_sixstack_ch8_init },
};


/* Macbook Air 2,1 */

static const struct hda_channel_mode alc885_mba21_ch_modes[1] = {
      { 2, NULL },
};

/*
 * macbook pro ALC885 can switch LineIn to LineOut without losing Mic
 */

/*
 * 2ch mode
 */
static const struct hda_verb alc885_mbp_ch2_init[] = {
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{ } /* end */
};

/*
 * 4ch mode
 */
static const struct hda_verb alc885_mbp_ch4_init[] = {
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{ } /* end */
};

static const struct hda_channel_mode alc885_mbp_4ch_modes[2] = {
	{ 2, alc885_mbp_ch2_init },
	{ 4, alc885_mbp_ch4_init },
};

/*
 * 2ch
 * Speakers/Woofer/HP = Front
 * LineIn = Input
 */
static const struct hda_verb alc885_mb5_ch2_init[] = {
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{ } /* end */
};

/*
 * 6ch mode
 * Speakers/HP = Front
 * Woofer = LFE
 * LineIn = Surround
 */
static const struct hda_verb alc885_mb5_ch6_init[] = {
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ } /* end */
};

static const struct hda_channel_mode alc885_mb5_6ch_modes[2] = {
	{ 2, alc885_mb5_ch2_init },
	{ 6, alc885_mb5_ch6_init },
};

#define alc885_macmini3_6ch_modes	alc885_mb5_6ch_modes

/*
 * 2ch mode
 */
static const struct hda_verb alc883_4ST_ch2_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 4ch mode
 */
static const struct hda_verb alc883_4ST_ch4_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

/*
 * 6ch mode
 */
static const struct hda_verb alc883_4ST_ch6_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

/*
 * 8ch mode
 */
static const struct hda_verb alc883_4ST_ch8_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x17, AC_VERB_SET_CONNECT_SEL, 0x03 },
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static const struct hda_channel_mode alc883_4ST_8ch_modes[4] = {
	{ 2, alc883_4ST_ch2_init },
	{ 4, alc883_4ST_ch4_init },
	{ 6, alc883_4ST_ch6_init },
	{ 8, alc883_4ST_ch8_init },
};


/*
 * 2ch mode
 */
static const struct hda_verb alc883_3ST_ch2_intel_init[] = {
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 4ch mode
 */
static const struct hda_verb alc883_3ST_ch4_intel_init[] = {
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

/*
 * 6ch mode
 */
static const struct hda_verb alc883_3ST_ch6_intel_init[] = {
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x19, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static const struct hda_channel_mode alc883_3ST_6ch_intel_modes[3] = {
	{ 2, alc883_3ST_ch2_intel_init },
	{ 4, alc883_3ST_ch4_intel_init },
	{ 6, alc883_3ST_ch6_intel_init },
};

/*
 * 2ch mode
 */
static const struct hda_verb alc889_ch2_intel_init[] = {
	{ 0x14, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x19, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x16, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x17, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 6ch mode
 */
static const struct hda_verb alc889_ch6_intel_init[] = {
	{ 0x14, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x19, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ 0x16, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x17, AC_VERB_SET_CONNECT_SEL, 0x03 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 8ch mode
 */
static const struct hda_verb alc889_ch8_intel_init[] = {
	{ 0x14, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x19, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ 0x16, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x17, AC_VERB_SET_CONNECT_SEL, 0x03 },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x03 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ } /* end */
};

static const struct hda_channel_mode alc889_8ch_intel_modes[3] = {
	{ 2, alc889_ch2_intel_init },
	{ 6, alc889_ch6_intel_init },
	{ 8, alc889_ch8_intel_init },
};

/*
 * 6ch mode
 */
static const struct hda_verb alc883_sixstack_ch6_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

/*
 * 8ch mode
 */
static const struct hda_verb alc883_sixstack_ch8_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

static const struct hda_channel_mode alc883_sixstack_modes[2] = {
	{ 6, alc883_sixstack_ch6_init },
	{ 8, alc883_sixstack_ch8_init },
};


/* Pin assignment: Front=0x14, Rear=0x15, CLFE=0x16, Side=0x17
 *                 Mic=0x18, Front Mic=0x19, Line-In=0x1a, HP=0x1b
 */
static const struct snd_kcontrol_new alc882_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

/* Macbook Air 2,1 same control for HP and internal Speaker */

static const struct snd_kcontrol_new alc885_mba21_mixer[] = {
      HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0c, 0x00, HDA_OUTPUT),
      HDA_BIND_MUTE("Speaker Playback Switch", 0x0c, 0x02, HDA_OUTPUT),
     { }
};


static const struct snd_kcontrol_new alc885_mbp3_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0c, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("Speaker Playback Switch", 0x0c, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0e, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("Headphone Playback Switch", 0x0e, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x00, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE  ("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE  ("Mic Playback Switch", 0x0b, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Boost Volume", 0x1a, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0x00, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc885_mb5_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("Front Playback Switch", 0x0c, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("Surround Playback Switch", 0x0d, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("LFE Playback Volume", 0x0e, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("LFE Playback Switch", 0x0e, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0f, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("Headphone Playback Switch", 0x0f, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x07, HDA_INPUT),
	HDA_CODEC_MUTE  ("Line Playback Switch", 0x0b, 0x07, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE  ("Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Boost Volume", 0x15, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x19, 0x00, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc885_macmini3_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("Front Playback Switch", 0x0c, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("Surround Playback Switch", 0x0d, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("LFE Playback Volume", 0x0e, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("LFE Playback Switch", 0x0e, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0f, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("Headphone Playback Switch", 0x0f, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x07, HDA_INPUT),
	HDA_CODEC_MUTE  ("Line Playback Switch", 0x0b, 0x07, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Boost Volume", 0x15, 0x00, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc885_imac91_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0c, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0c, 0x02, HDA_INPUT),
	{ } /* end */
};


static const struct snd_kcontrol_new alc882_w2jc_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc882_targa_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	{ } /* end */
};

/* Pin assignment: Front=0x14, HP = 0x15, Front = 0x16, ???
 *                 Front Mic=0x18, Line In = 0x1a, Line In = 0x1b, CD = 0x1c
 */
static const struct snd_kcontrol_new alc882_asus_a7j_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mobile Front Playback Switch", 0x16, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mobile Line Playback Volume", 0x0b, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("Mobile Line Playback Switch", 0x0b, 0x03, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc882_asus_a7m_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc882_chmode_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static const struct hda_verb alc882_base_init_verbs[] = {
	/* Front mixer: unmute input/output amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Rear mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* CLFE mixer */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Side mixer */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	/* Front Pin: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Rear Pin: output 1 (0x0d) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	/* CLFE Pin: output 2 (0x0e) */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* Side Pin: output 3 (0x0f) */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x03},
	/* Mic (rear) pin: input vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line In pin: input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line-2 In: Headphone output (output 0 - 0x0c) */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* ADC2: mute amp left and right */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC3: mute amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},

	{ }
};

static const struct hda_verb alc882_adc1_init_verbs[] = {
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* ADC1: mute amp left and right */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{ }
};

static const struct hda_verb alc882_eapd_verbs[] = {
	/* change to EAPD mode */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF, 0x3060},
	{ }
};

static const struct hda_verb alc889_eapd_verbs[] = {
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

static const struct hda_verb alc_hp15_unsol_verbs[] = {
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_HP_EVENT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{}
};

static const struct hda_verb alc885_init_verbs[] = {
	/* Front mixer: unmute input/output amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* Rear mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* CLFE mixer */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* Side mixer */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* Front HP Pin: output 0 (0x0c) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Front Pin: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Rear Pin: output 1 (0x0d) */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x19, AC_VERB_SET_CONNECT_SEL, 0x01},
	/* CLFE Pin: output 2 (0x0e) */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* Side Pin: output 3 (0x0f) */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x03},
	/* Mic (rear) pin: input vref at 80% */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Front Mic pin: input vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line In pin: input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	/* Mixer elements: 0x18, , 0x1a, 0x1b */
	/* Input mixer1 */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* ADC2: mute amp left and right */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	/* ADC3: mute amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},

	{ }
};

static const struct hda_verb alc885_init_input_verbs[] = {
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{ }
};


/* Unmute Selector 24h and set the default input to front mic */
static const struct hda_verb alc889_init_input_verbs[] = {
	{0x24, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{ }
};


#define alc883_init_verbs	alc882_base_init_verbs

/* Mac Pro test */
static const struct snd_kcontrol_new alc882_macpro_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x18, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x01, HDA_INPUT),
	/* FIXME: this looks suspicious...
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Beep Playback Switch", 0x0b, 0x02, HDA_INPUT),
	*/
	{ } /* end */
};

static const struct hda_verb alc882_macpro_init_verbs[] = {
	/* Front mixer: unmute input/output amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Front Pin: output 0 (0x0c) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Speaker:  output */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x04},
	/* Headphone output (output 0 - 0x0c) */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* ADC1: mute amp left and right */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC2: mute amp left and right */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC3: mute amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},

	{ }
};

/* Macbook 5,1 */
static const struct hda_verb alc885_mb5_init_verbs[] = {
	/* DACs */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Front mixer */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Surround mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* LFE mixer */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* HP mixer */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Front Pin (0x0c) */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x01},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* LFE Pin (0x0e) */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x01},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* HP Pin (0x0f) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x03},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line In pin */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0x1)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0x7)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0x4)},
	{ }
};

/* Macmini 3,1 */
static const struct hda_verb alc885_macmini3_init_verbs[] = {
	/* DACs */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Front mixer */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Surround mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* LFE mixer */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* HP mixer */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Front Pin (0x0c) */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x01},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* LFE Pin (0x0e) */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x01},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* HP Pin (0x0f) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x03},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
	/* Line In pin */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{ }
};


static const struct hda_verb alc885_mba21_init_verbs[] = {
	/*Internal and HP Speaker Mixer*/
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/*Internal Speaker Pin (0x0c)*/
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, (PIN_OUT | AC_PINCTL_VREF_50) },
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* HP Pin: output 0 (0x0e) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc4},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, (ALC_HP_EVENT | AC_USRSP_EN)},
	/* Line in (is hp when jack connected)*/
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, AC_PINCTL_VREF_50},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{ }
 };


/* Macbook Pro rev3 */
static const struct hda_verb alc885_mbp3_init_verbs[] = {
	/* Front mixer: unmute input/output amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Rear mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* HP mixer */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Front Pin: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* HP Pin: output 0 (0x0e) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc4},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x02},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
	/* Mic (rear) pin: input vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line In pin: use output 1 when in LineOut mode */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* ADC1: mute amp left and right */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC2: mute amp left and right */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC3: mute amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},

	{ }
};

/* iMac 9,1 */
static const struct hda_verb alc885_imac91_init_verbs[] = {
	/* Internal Speaker Pin (0x0c) */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, (PIN_OUT | AC_PINCTL_VREF_50) },
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, (PIN_OUT | AC_PINCTL_VREF_50) },
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* HP Pin: Rear */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, (ALC_HP_EVENT | AC_USRSP_EN)},
	/* Line in Rear */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, AC_PINCTL_VREF_50},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Rear mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Line-Out mixer: unmute input/output amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* 0x24 [Audio Mixer] wcaps 0x20010b: Stereo Amp-In */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* 0x23 [Audio Mixer] wcaps 0x20010b: Stereo Amp-In */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* 0x22 [Audio Mixer] wcaps 0x20010b: Stereo Amp-In */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* 0x07 [Audio Input] wcaps 0x10011b: Stereo Amp-In */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* 0x08 [Audio Input] wcaps 0x10011b: Stereo Amp-In */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* 0x09 [Audio Input] wcaps 0x10011b: Stereo Amp-In */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{ }
};

/* iMac 24 mixer. */
static const struct snd_kcontrol_new alc885_imac24_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x0c, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("Master Playback Switch", 0x0c, 0x00, HDA_INPUT),
	{ } /* end */
};

/* iMac 24 init verbs. */
static const struct hda_verb alc885_imac24_init_verbs[] = {
	/* Internal speakers: output 0 (0x0c) */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Internal speakers: output 0 (0x0c) */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Headphone: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
	/* Front Mic: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{ }
};

/* Toggle speaker-output according to the hp-jack state */
static void alc885_imac24_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x18;
	spec->autocfg.speaker_pins[1] = 0x1a;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

#define alc885_mb5_setup	alc885_imac24_setup
#define alc885_macmini3_setup	alc885_imac24_setup

/* Macbook Air 2,1 */
static void alc885_mba21_setup(struct hda_codec *codec)
{
       struct alc_spec *spec = codec->spec;

       spec->autocfg.hp_pins[0] = 0x14;
       spec->autocfg.speaker_pins[0] = 0x18;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}



static void alc885_mbp3_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc885_imac91_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x18;
	spec->autocfg.speaker_pins[1] = 0x1a;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static const struct hda_verb alc882_targa_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02}, /* mic/clfe */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01}, /* line/surround */
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
	{ } /* end */
};

/* toggle speaker-output according to the hp-jack state */
static void alc882_targa_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc_hp_automute(codec);
	snd_hda_codec_write_cache(codec, 1, 0, AC_VERB_SET_GPIO_DATA,
				  spec->jack_present ? 1 : 3);
}

static void alc882_targa_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x1b;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc882_targa_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if ((res >> 26) == ALC_HP_EVENT)
		alc882_targa_automute(codec);
}

static const struct hda_verb alc882_asus_a7j_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00}, /* Front */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x00}, /* Front */

	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02}, /* mic/clfe */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01}, /* line/surround */
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
	{ } /* end */
};

static const struct hda_verb alc882_asus_a7m_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00}, /* Front */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x00}, /* Front */

	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02}, /* mic/clfe */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01}, /* line/surround */
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
 	{ } /* end */
};

static void alc882_gpio_mute(struct hda_codec *codec, int pin, int muted)
{
	unsigned int gpiostate, gpiomask, gpiodir;

	gpiostate = snd_hda_codec_read(codec, codec->afg, 0,
				       AC_VERB_GET_GPIO_DATA, 0);

	if (!muted)
		gpiostate |= (1 << pin);
	else
		gpiostate &= ~(1 << pin);

	gpiomask = snd_hda_codec_read(codec, codec->afg, 0,
				      AC_VERB_GET_GPIO_MASK, 0);
	gpiomask |= (1 << pin);

	gpiodir = snd_hda_codec_read(codec, codec->afg, 0,
				     AC_VERB_GET_GPIO_DIRECTION, 0);
	gpiodir |= (1 << pin);


	snd_hda_codec_write(codec, codec->afg, 0,
			    AC_VERB_SET_GPIO_MASK, gpiomask);
	snd_hda_codec_write(codec, codec->afg, 0,
			    AC_VERB_SET_GPIO_DIRECTION, gpiodir);

	msleep(1);

	snd_hda_codec_write(codec, codec->afg, 0,
			    AC_VERB_SET_GPIO_DATA, gpiostate);
}

/* set up GPIO at initialization */
static void alc885_macpro_init_hook(struct hda_codec *codec)
{
	alc882_gpio_mute(codec, 0, 0);
	alc882_gpio_mute(codec, 1, 0);
}

/* set up GPIO and update auto-muting at initialization */
static void alc885_imac24_init_hook(struct hda_codec *codec)
{
	alc885_macpro_init_hook(codec);
	alc_hp_automute(codec);
}

/* 2ch mode (Speaker:front, Subwoofer:CLFE, Line:input, Headphones:front) */
static const struct hda_verb alc889A_mb31_ch2_init[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},             /* HP as front */
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, /* Subwoofer on */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},    /* Line as input */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},   /* Line off */
	{ } /* end */
};

/* 4ch mode (Speaker:front, Subwoofer:CLFE, Line:CLFE, Headphones:front) */
static const struct hda_verb alc889A_mb31_ch4_init[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},             /* HP as front */
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, /* Subwoofer on */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},   /* Line as output */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, /* Line on */
	{ } /* end */
};

/* 5ch mode (Speaker:front, Subwoofer:CLFE, Line:input, Headphones:rear) */
static const struct hda_verb alc889A_mb31_ch5_init[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},             /* HP as rear */
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, /* Subwoofer on */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},    /* Line as input */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},   /* Line off */
	{ } /* end */
};

/* 6ch mode (Speaker:front, Subwoofer:off, Line:CLFE, Headphones:Rear) */
static const struct hda_verb alc889A_mb31_ch6_init[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},             /* HP as front */
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},   /* Subwoofer off */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},   /* Line as output */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, /* Line on */
	{ } /* end */
};

static const struct hda_channel_mode alc889A_mb31_6ch_modes[4] = {
	{ 2, alc889A_mb31_ch2_init },
	{ 4, alc889A_mb31_ch4_init },
	{ 5, alc889A_mb31_ch5_init },
	{ 6, alc889A_mb31_ch6_init },
};

static const struct hda_verb alc883_medion_eapd_verbs[] = {
        /* eanable EAPD on medion laptop */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF, 0x3070},
	{ }
};

#define alc883_base_mixer	alc882_base_mixer

static const struct snd_kcontrol_new alc883_mitac_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_clevo_m720_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_2ch_fujitsu_pi2515_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_3ST_2ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_3ST_6ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_3ST_6ch_intel_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0,
			      HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc885_8ch_intel_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0,
			      HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x1b, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_fivestack_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_targa_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_targa_2ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_targa_8ch_mixer[] = {
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_lenovo_101e_2ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_lenovo_nb0763_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_medion_wim2160_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1a, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x08, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct hda_verb alc883_medion_wim2160_verbs[] = {
	/* Unmute front mixer */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* Set speaker pin to front mixer */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Init headphone pin */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x1a, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},

	{ } /* end */
};

/* toggle speaker-output according to the hp-jack state */
static void alc883_medion_wim2160_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1a;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static const struct snd_kcontrol_new alc883_acer_aspire_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc888_acer_aspire_6530_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("LFE Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc888_lenovo_sky_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0e, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0e, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume",
						0x0d, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0d, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0d, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0d, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc889A_mb31_mixer[] = {
	/* Output mixers */
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x00,
		HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 0x02, HDA_INPUT),
	/* Output switches */
	HDA_CODEC_MUTE("Enable Speaker", 0x14, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("Enable Headphones", 0x15, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Enable LFE", 0x16, 2, 0x00, HDA_OUTPUT),
	/* Boost mixers */
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Boost Volume", 0x1a, 0x00, HDA_INPUT),
	/* Input mixers */
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_vaiott_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct hda_bind_ctls alc883_bind_cap_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x08, 3, 0, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x09, 3, 0, HDA_INPUT),
		0
	},
};

static const struct hda_bind_ctls alc883_bind_cap_switch = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x08, 3, 0, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x09, 3, 0, HDA_INPUT),
		0
	},
};

static const struct snd_kcontrol_new alc883_asus_eee1601_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_asus_eee1601_cap_mixer[] = {
	HDA_BIND_VOL("Capture Volume", &alc883_bind_cap_vol),
	HDA_BIND_SW("Capture Switch", &alc883_bind_cap_switch),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_chmode_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

/* toggle speaker-output according to the hp-jack state */
static void alc883_mitac_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x17;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static const struct hda_verb alc883_mitac_verbs[] = {
	/* HP */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Subwoofer */
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x02},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* enable unsolicited event */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
	/* {0x18, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_MIC_EVENT | AC_USRSP_EN}, */

	{ } /* end */
};

static const struct hda_verb alc883_clevo_m540r_verbs[] = {
	/* HP */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Int speaker */
	/*{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},*/

	/* enable unsolicited event */
	/*
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_MIC_EVENT | AC_USRSP_EN},
	*/

	{ } /* end */
};

static const struct hda_verb alc883_clevo_m720_verbs[] = {
	/* HP */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Int speaker */
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* enable unsolicited event */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_MIC_EVENT | AC_USRSP_EN},

	{ } /* end */
};

static const struct hda_verb alc883_2ch_fujitsu_pi2515_verbs[] = {
	/* HP */
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Subwoofer */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* enable unsolicited event */
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},

	{ } /* end */
};

static const struct hda_verb alc883_targa_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

/* Connect Line-Out side jack (SPDIF) to Side */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x03},
/* Connect Mic jack to CLFE */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02},
/* Connect Line-in jack to Surround */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01},
/* Connect HP out jack to Front */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},

	{ } /* end */
};

static const struct hda_verb alc883_lenovo_101e_verbs[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_FRONT_EVENT|AC_USRSP_EN},
        {0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT|AC_USRSP_EN},
	{ } /* end */
};

static const struct hda_verb alc883_lenovo_nb0763_verbs[] = {
        {0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
        {0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
        {0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{ } /* end */
};

static const struct hda_verb alc888_lenovo_ms7195_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_FRONT_EVENT | AC_USRSP_EN},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT    | AC_USRSP_EN},
	{ } /* end */
};

static const struct hda_verb alc883_haier_w66_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{ } /* end */
};

static const struct hda_verb alc888_lenovo_sky_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
	{ } /* end */
};

static const struct hda_verb alc888_6st_dell_verbs[] = {
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
	{ }
};

static const struct hda_verb alc883_vaiott_verbs[] = {
	/* HP */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},

	/* enable unsolicited event */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},

	{ } /* end */
};

static void alc888_3st_hp_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x16;
	spec->autocfg.speaker_pins[2] = 0x18;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static const struct hda_verb alc888_3st_hp_verbs[] = {
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},	/* Front: output 0 (0x0c) */
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x01},	/* Rear : output 1 (0x0d) */
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02},	/* CLFE : output 2 (0x0e) */
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
	{ } /* end */
};

/*
 * 2ch mode
 */
static const struct hda_verb alc888_3st_hp_2ch_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 4ch mode
 */
static const struct hda_verb alc888_3st_hp_4ch_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x16, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

/*
 * 6ch mode
 */
static const struct hda_verb alc888_3st_hp_6ch_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x16, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static const struct hda_channel_mode alc888_3st_hp_modes[3] = {
	{ 2, alc888_3st_hp_2ch_init },
	{ 4, alc888_3st_hp_4ch_init },
	{ 6, alc888_3st_hp_6ch_init },
};

static void alc888_lenovo_ms7195_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.line_out_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

/* toggle speaker-output according to the hp-jack state */
static void alc883_lenovo_nb0763_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

/* toggle speaker-output according to the hp-jack state */
#define alc883_targa_init_hook		alc882_targa_init_hook
#define alc883_targa_unsol_event	alc882_targa_unsol_event

static void alc883_clevo_m720_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc883_clevo_m720_init_hook(struct hda_codec *codec)
{
	alc_hp_automute(codec);
	alc88x_simple_mic_automute(codec);
}

static void alc883_clevo_m720_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC_MIC_EVENT:
		alc88x_simple_mic_automute(codec);
		break;
	default:
		alc_sku_unsol_event(codec, res);
		break;
	}
}

/* toggle speaker-output according to the hp-jack state */
static void alc883_2ch_fujitsu_pi2515_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc883_haier_w66_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc883_lenovo_101e_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.line_out_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->automute = 1;
	spec->detect_line = 1;
	spec->automute_lines = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

/* toggle speaker-output according to the hp-jack state */
static void alc883_acer_aspire_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->autocfg.speaker_pins[1] = 0x16;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static const struct hda_verb alc883_acer_eapd_verbs[] = {
	/* HP Pin: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Front Pin: output 0 (0x0c) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x00},
        /* eanable EAPD on medion laptop */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF, 0x3050},
	/* enable unsolicited event */
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
	{ }
};

static void alc888_6st_dell_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x15;
	spec->autocfg.speaker_pins[2] = 0x16;
	spec->autocfg.speaker_pins[3] = 0x17;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc888_lenovo_sky_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x15;
	spec->autocfg.speaker_pins[2] = 0x16;
	spec->autocfg.speaker_pins[3] = 0x17;
	spec->autocfg.speaker_pins[4] = 0x1a;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc883_vaiott_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x17;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static const struct hda_verb alc888_asus_m90v_verbs[] = {
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* enable unsolicited event */
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_MIC_EVENT | AC_USRSP_EN},
	{ } /* end */
};

static void alc883_mode2_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x15;
	spec->autocfg.speaker_pins[2] = 0x16;
	spec->ext_mic_pin = 0x18;
	spec->int_mic_pin = 0x19;
	spec->auto_mic = 1;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static const struct hda_verb alc888_asus_eee1601_verbs[] = {
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x20, AC_VERB_SET_COEF_INDEX, 0x0b},
	{0x20, AC_VERB_SET_PROC_COEF,  0x0838},
	/* enable unsolicited event */
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
	{ } /* end */
};

static void alc883_eee1601_inithook(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x1b;
	alc_hp_automute(codec);
}

static const struct hda_verb alc889A_mb31_verbs[] = {
	/* Init rear pin (used as headphone output) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc4},    /* Apple Headphones */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},           /* Connect to front */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC_HP_EVENT | AC_USRSP_EN},
	/* Init line pin (used as output in 4ch and 6ch mode) */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x02},           /* Connect to CLFE */
	/* Init line 2 pin (used as headphone out by default) */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},  /* Use as input */
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE}, /* Mute output */
	{ } /* end */
};

/* Mute speakers according to the headphone jack state */
static void alc889A_mb31_automute(struct hda_codec *codec)
{
	unsigned int present;

	/* Mute only in 2ch or 4ch mode */
	if (snd_hda_codec_read(codec, 0x15, 0, AC_VERB_GET_CONNECT_SEL, 0)
	    == 0x00) {
		present = snd_hda_jack_detect(codec, 0x15);
		snd_hda_codec_amp_stereo(codec, 0x14,  HDA_OUTPUT, 0,
			HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
		snd_hda_codec_amp_stereo(codec, 0x16, HDA_OUTPUT, 0,
			HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	}
}

static void alc889A_mb31_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if ((res >> 26) == ALC_HP_EVENT)
		alc889A_mb31_automute(codec);
}

static const hda_nid_t alc883_slave_dig_outs[] = {
	ALC1200_DIGOUT_NID, 0,
};

static const hda_nid_t alc1200_slave_dig_outs[] = {
	ALC883_DIGOUT_NID, 0,
};

/*
 * configuration and preset
 */
static const char * const alc882_models[ALC882_MODEL_LAST] = {
	[ALC882_3ST_DIG]	= "3stack-dig",
	[ALC882_6ST_DIG]	= "6stack-dig",
	[ALC882_ARIMA]		= "arima",
	[ALC882_W2JC]		= "w2jc",
	[ALC882_TARGA]		= "targa",
	[ALC882_ASUS_A7J]	= "asus-a7j",
	[ALC882_ASUS_A7M]	= "asus-a7m",
	[ALC885_MACPRO]		= "macpro",
	[ALC885_MB5]		= "mb5",
	[ALC885_MACMINI3]	= "macmini3",
	[ALC885_MBA21]		= "mba21",
	[ALC885_MBP3]		= "mbp3",
	[ALC885_IMAC24]		= "imac24",
	[ALC885_IMAC91]		= "imac91",
	[ALC883_3ST_2ch_DIG]	= "3stack-2ch-dig",
	[ALC883_3ST_6ch_DIG]	= "3stack-6ch-dig",
	[ALC883_3ST_6ch]	= "3stack-6ch",
	[ALC883_6ST_DIG]	= "alc883-6stack-dig",
	[ALC883_TARGA_DIG]	= "targa-dig",
	[ALC883_TARGA_2ch_DIG]	= "targa-2ch-dig",
	[ALC883_TARGA_8ch_DIG]	= "targa-8ch-dig",
	[ALC883_ACER]		= "acer",
	[ALC883_ACER_ASPIRE]	= "acer-aspire",
	[ALC888_ACER_ASPIRE_4930G]	= "acer-aspire-4930g",
	[ALC888_ACER_ASPIRE_6530G]	= "acer-aspire-6530g",
	[ALC888_ACER_ASPIRE_8930G]	= "acer-aspire-8930g",
	[ALC888_ACER_ASPIRE_7730G]	= "acer-aspire-7730g",
	[ALC883_MEDION]		= "medion",
	[ALC883_MEDION_WIM2160]	= "medion-wim2160",
	[ALC883_LAPTOP_EAPD]	= "laptop-eapd",
	[ALC883_LENOVO_101E_2ch] = "lenovo-101e",
	[ALC883_LENOVO_NB0763]	= "lenovo-nb0763",
	[ALC888_LENOVO_MS7195_DIG] = "lenovo-ms7195-dig",
	[ALC888_LENOVO_SKY] = "lenovo-sky",
	[ALC883_HAIER_W66] 	= "haier-w66",
	[ALC888_3ST_HP]		= "3stack-hp",
	[ALC888_6ST_DELL]	= "6stack-dell",
	[ALC883_MITAC]		= "mitac",
	[ALC883_CLEVO_M540R]	= "clevo-m540r",
	[ALC883_CLEVO_M720]	= "clevo-m720",
	[ALC883_FUJITSU_PI2515] = "fujitsu-pi2515",
	[ALC888_FUJITSU_XA3530] = "fujitsu-xa3530",
	[ALC883_3ST_6ch_INTEL]	= "3stack-6ch-intel",
	[ALC889A_INTEL]		= "intel-alc889a",
	[ALC889_INTEL]		= "intel-x58",
	[ALC1200_ASUS_P5Q]	= "asus-p5q",
	[ALC889A_MB31]		= "mb31",
	[ALC883_SONY_VAIO_TT]	= "sony-vaio-tt",
	[ALC882_AUTO]		= "auto",
};

static const struct snd_pci_quirk alc882_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0x6668, "ECS", ALC882_6ST_DIG),

	SND_PCI_QUIRK(0x1025, 0x006c, "Acer Aspire 9810", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x0090, "Acer Aspire", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x010a, "Acer Ferrari 5000", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x0110, "Acer Aspire", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x0112, "Acer Aspire 9303", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x0121, "Acer Aspire 5920G", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x013e, "Acer Aspire 4930G",
		ALC888_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x013f, "Acer Aspire 5930G",
		ALC888_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0145, "Acer Aspire 8930G",
		ALC888_ACER_ASPIRE_8930G),
	SND_PCI_QUIRK(0x1025, 0x0146, "Acer Aspire 6935G",
		ALC888_ACER_ASPIRE_8930G),
	SND_PCI_QUIRK(0x1025, 0x0157, "Acer X3200", ALC882_AUTO),
	SND_PCI_QUIRK(0x1025, 0x0158, "Acer AX1700-U3700A", ALC882_AUTO),
	SND_PCI_QUIRK(0x1025, 0x015e, "Acer Aspire 6930G",
		ALC888_ACER_ASPIRE_6530G),
	SND_PCI_QUIRK(0x1025, 0x0166, "Acer Aspire 6530G",
		ALC888_ACER_ASPIRE_6530G),
	SND_PCI_QUIRK(0x1025, 0x0142, "Acer Aspire 7730G",
		ALC888_ACER_ASPIRE_7730G),
	/* default Acer -- disabled as it causes more problems.
	 *    model=auto should work fine now
	 */
	/* SND_PCI_QUIRK_VENDOR(0x1025, "Acer laptop", ALC883_ACER), */

	SND_PCI_QUIRK(0x1028, 0x020d, "Dell Inspiron 530", ALC888_6ST_DELL),

	SND_PCI_QUIRK(0x103c, 0x2a3d, "HP Pavilion", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x103c, 0x2a4f, "HP Samba", ALC888_3ST_HP),
	SND_PCI_QUIRK(0x103c, 0x2a60, "HP Lucknow", ALC888_3ST_HP),
	SND_PCI_QUIRK(0x103c, 0x2a61, "HP Nettle", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x103c, 0x2a66, "HP Acacia", ALC888_3ST_HP),
	SND_PCI_QUIRK(0x103c, 0x2a72, "HP Educ.ar", ALC888_3ST_HP),

	SND_PCI_QUIRK(0x1043, 0x060d, "Asus A7J", ALC882_ASUS_A7J),
	SND_PCI_QUIRK(0x1043, 0x1243, "Asus A7J", ALC882_ASUS_A7J),
	SND_PCI_QUIRK(0x1043, 0x13c2, "Asus A7M", ALC882_ASUS_A7M),
	SND_PCI_QUIRK(0x1043, 0x1873, "Asus M90V", ALC888_ASUS_M90V),
	SND_PCI_QUIRK(0x1043, 0x1971, "Asus W2JC", ALC882_W2JC),
	SND_PCI_QUIRK(0x1043, 0x817f, "Asus P5LD2", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x1043, 0x81d8, "Asus P5WD", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x1043, 0x8249, "Asus M2A-VM HDMI", ALC883_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1043, 0x8284, "Asus Z37E", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1043, 0x82fe, "Asus P5Q-EM HDMI", ALC1200_ASUS_P5Q),
	SND_PCI_QUIRK(0x1043, 0x835f, "Asus Eee 1601", ALC888_ASUS_EEE1601),

	SND_PCI_QUIRK(0x104d, 0x9047, "Sony Vaio TT", ALC883_SONY_VAIO_TT),
	SND_PCI_QUIRK(0x105b, 0x0ce8, "Foxconn P35AX-S", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x105b, 0x6668, "Foxconn", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x1071, 0x8227, "Mitac 82801H", ALC883_MITAC),
	SND_PCI_QUIRK(0x1071, 0x8253, "Mitac 8252d", ALC883_MITAC),
	SND_PCI_QUIRK(0x1071, 0x8258, "Evesham Voyaeger", ALC883_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x10f1, 0x2350, "TYAN-S2350", ALC888_6ST_DELL),
	SND_PCI_QUIRK(0x108e, 0x534d, NULL, ALC883_3ST_6ch),
	SND_PCI_QUIRK(0x1458, 0xa002, "Gigabyte P35 DS3R", ALC882_6ST_DIG),

	SND_PCI_QUIRK(0x1462, 0x0349, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x040d, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x0579, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x28fb, "Targa T8", ALC882_TARGA), /* MSI-1049 T8  */
	SND_PCI_QUIRK(0x1462, 0x2fb3, "MSI", ALC882_AUTO),
	SND_PCI_QUIRK(0x1462, 0x6668, "MSI", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x3729, "MSI S420", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3783, "NEC S970", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3b7f, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x3ef9, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3fc1, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3fc3, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3fcc, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3fdf, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x42cd, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x4314, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x4319, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x4324, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x4570, "MSI Wind Top AE2220", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x6510, "MSI GX620", ALC883_TARGA_8ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x6668, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7187, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7250, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7260, "MSI 7260", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x7267, "MSI", ALC883_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x7280, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7327, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7350, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7437, "MSI NetOn AP1900", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0xa422, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0xaa08, "MSI", ALC883_TARGA_2ch_DIG),

	SND_PCI_QUIRK(0x147b, 0x1083, "Abit IP35-PRO", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1558, 0x0571, "Clevo laptop M570U", ALC883_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1558, 0x0721, "Clevo laptop M720R", ALC883_CLEVO_M720),
	SND_PCI_QUIRK(0x1558, 0x0722, "Clevo laptop M720SR", ALC883_CLEVO_M720),
	SND_PCI_QUIRK(0x1558, 0x5409, "Clevo laptop M540R", ALC883_CLEVO_M540R),
	SND_PCI_QUIRK_VENDOR(0x1558, "Clevo laptop", ALC883_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x15d9, 0x8780, "Supermicro PDSBA", ALC883_3ST_6ch),
	/* SND_PCI_QUIRK(0x161f, 0x2054, "Arima W820", ALC882_ARIMA), */
	SND_PCI_QUIRK(0x161f, 0x2054, "Medion laptop", ALC883_MEDION),
	SND_PCI_QUIRK_MASK(0x1734, 0xfff0, 0x1100, "FSC AMILO Xi/Pi25xx",
		      ALC883_FUJITSU_PI2515),
	SND_PCI_QUIRK_MASK(0x1734, 0xfff0, 0x1130, "Fujitsu AMILO Xa35xx",
		ALC888_FUJITSU_XA3530),
	SND_PCI_QUIRK(0x17aa, 0x101e, "Lenovo 101e", ALC883_LENOVO_101E_2ch),
	SND_PCI_QUIRK(0x17aa, 0x2085, "Lenovo NB0763", ALC883_LENOVO_NB0763),
	SND_PCI_QUIRK(0x17aa, 0x3bfc, "Lenovo NB0763", ALC883_LENOVO_NB0763),
	SND_PCI_QUIRK(0x17aa, 0x3bfd, "Lenovo NB0763", ALC883_LENOVO_NB0763),
	SND_PCI_QUIRK(0x17aa, 0x101d, "Lenovo Sky", ALC888_LENOVO_SKY),
	SND_PCI_QUIRK(0x17c0, 0x4085, "MEDION MD96630", ALC888_LENOVO_MS7195_DIG),
	SND_PCI_QUIRK(0x17f2, 0x5000, "Albatron KI690-AM2", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1991, 0x5625, "Haier W66", ALC883_HAIER_W66),

	SND_PCI_QUIRK(0x8086, 0x0001, "DG33BUC", ALC883_3ST_6ch_INTEL),
	SND_PCI_QUIRK(0x8086, 0x0002, "DG33FBC", ALC883_3ST_6ch_INTEL),
	SND_PCI_QUIRK(0x8086, 0x2503, "82801H", ALC883_MITAC),
	SND_PCI_QUIRK(0x8086, 0x0022, "DX58SO", ALC889_INTEL),
	SND_PCI_QUIRK(0x8086, 0x0021, "Intel IbexPeak", ALC889A_INTEL),
	SND_PCI_QUIRK(0x8086, 0x3b56, "Intel IbexPeak", ALC889A_INTEL),
	SND_PCI_QUIRK(0x8086, 0xd601, "D102GGC", ALC882_6ST_DIG),

	{}
};

/* codec SSID table for Intel Mac */
static const struct snd_pci_quirk alc882_ssid_cfg_tbl[] = {
	SND_PCI_QUIRK(0x106b, 0x00a0, "MacBookPro 3,1", ALC885_MBP3),
	SND_PCI_QUIRK(0x106b, 0x00a1, "Macbook", ALC885_MBP3),
	SND_PCI_QUIRK(0x106b, 0x00a4, "MacbookPro 4,1", ALC885_MBP3),
	SND_PCI_QUIRK(0x106b, 0x0c00, "Mac Pro", ALC885_MACPRO),
	SND_PCI_QUIRK(0x106b, 0x1000, "iMac 24", ALC885_IMAC24),
	SND_PCI_QUIRK(0x106b, 0x2800, "AppleTV", ALC885_IMAC24),
	SND_PCI_QUIRK(0x106b, 0x2c00, "MacbookPro rev3", ALC885_MBP3),
	SND_PCI_QUIRK(0x106b, 0x3000, "iMac", ALC889A_MB31),
	SND_PCI_QUIRK(0x106b, 0x3200, "iMac 7,1 Aluminum", ALC882_ASUS_A7M),
	SND_PCI_QUIRK(0x106b, 0x3400, "MacBookAir 1,1", ALC885_MBP3),
	SND_PCI_QUIRK(0x106b, 0x3500, "MacBookAir 2,1", ALC885_MBA21),
	SND_PCI_QUIRK(0x106b, 0x3600, "Macbook 3,1", ALC889A_MB31),
	SND_PCI_QUIRK(0x106b, 0x3800, "MacbookPro 4,1", ALC885_MBP3),
	SND_PCI_QUIRK(0x106b, 0x3e00, "iMac 24 Aluminum", ALC885_IMAC24),
	SND_PCI_QUIRK(0x106b, 0x4900, "iMac 9,1 Aluminum", ALC885_IMAC91),
	SND_PCI_QUIRK(0x106b, 0x3f00, "Macbook 5,1", ALC885_MB5),
	SND_PCI_QUIRK(0x106b, 0x4a00, "Macbook 5,2", ALC885_MB5),
	/* FIXME: HP jack sense seems not working for MBP 5,1 or 5,2,
	 * so apparently no perfect solution yet
	 */
	SND_PCI_QUIRK(0x106b, 0x4000, "MacbookPro 5,1", ALC885_MB5),
	SND_PCI_QUIRK(0x106b, 0x4600, "MacbookPro 5,2", ALC885_MB5),
	SND_PCI_QUIRK(0x106b, 0x4100, "Macmini 3,1", ALC885_MACMINI3),
	{} /* terminator */
};

static const struct alc_config_preset alc882_presets[] = {
	[ALC882_3ST_DIG] = {
		.mixers = { alc882_base_mixer },
		.init_verbs = { alc882_base_init_verbs,
				alc882_adc1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc882_ch_modes),
		.channel_mode = alc882_ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc882_capture_source,
	},
	[ALC882_6ST_DIG] = {
		.mixers = { alc882_base_mixer, alc882_chmode_mixer },
		.init_verbs = { alc882_base_init_verbs,
				alc882_adc1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc882_sixstack_modes),
		.channel_mode = alc882_sixstack_modes,
		.input_mux = &alc882_capture_source,
	},
	[ALC882_ARIMA] = {
		.mixers = { alc882_base_mixer, alc882_chmode_mixer },
		.init_verbs = { alc882_base_init_verbs, alc882_adc1_init_verbs,
				alc882_eapd_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc882_sixstack_modes),
		.channel_mode = alc882_sixstack_modes,
		.input_mux = &alc882_capture_source,
	},
	[ALC882_W2JC] = {
		.mixers = { alc882_w2jc_mixer, alc882_chmode_mixer },
		.init_verbs = { alc882_base_init_verbs, alc882_adc1_init_verbs,
				alc882_eapd_verbs, alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc882_capture_source,
		.dig_out_nid = ALC882_DIGOUT_NID,
	},
	   [ALC885_MBA21] = {
			.mixers = { alc885_mba21_mixer },
			.init_verbs = { alc885_mba21_init_verbs, alc880_gpio1_init_verbs },
			.num_dacs = 2,
			.dac_nids = alc882_dac_nids,
			.channel_mode = alc885_mba21_ch_modes,
			.num_channel_mode = ARRAY_SIZE(alc885_mba21_ch_modes),
			.input_mux = &alc882_capture_source,
			.unsol_event = alc_sku_unsol_event,
			.setup = alc885_mba21_setup,
			.init_hook = alc_hp_automute,
       },
	[ALC885_MBP3] = {
		.mixers = { alc885_mbp3_mixer, alc882_chmode_mixer },
		.init_verbs = { alc885_mbp3_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = 2,
		.dac_nids = alc882_dac_nids,
		.hp_nid = 0x04,
		.channel_mode = alc885_mbp_4ch_modes,
		.num_channel_mode = ARRAY_SIZE(alc885_mbp_4ch_modes),
		.input_mux = &alc882_capture_source,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc885_mbp3_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC885_MB5] = {
		.mixers = { alc885_mb5_mixer, alc882_chmode_mixer },
		.init_verbs = { alc885_mb5_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.channel_mode = alc885_mb5_6ch_modes,
		.num_channel_mode = ARRAY_SIZE(alc885_mb5_6ch_modes),
		.input_mux = &mb5_capture_source,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc885_mb5_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC885_MACMINI3] = {
		.mixers = { alc885_macmini3_mixer, alc882_chmode_mixer },
		.init_verbs = { alc885_macmini3_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.channel_mode = alc885_macmini3_6ch_modes,
		.num_channel_mode = ARRAY_SIZE(alc885_macmini3_6ch_modes),
		.input_mux = &macmini3_capture_source,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc885_macmini3_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC885_MACPRO] = {
		.mixers = { alc882_macpro_mixer },
		.init_verbs = { alc882_macpro_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc882_ch_modes),
		.channel_mode = alc882_ch_modes,
		.input_mux = &alc882_capture_source,
		.init_hook = alc885_macpro_init_hook,
	},
	[ALC885_IMAC24] = {
		.mixers = { alc885_imac24_mixer },
		.init_verbs = { alc885_imac24_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc882_ch_modes),
		.channel_mode = alc882_ch_modes,
		.input_mux = &alc882_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc885_imac24_setup,
		.init_hook = alc885_imac24_init_hook,
	},
	[ALC885_IMAC91] = {
		.mixers = {alc885_imac91_mixer},
		.init_verbs = { alc885_imac91_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.channel_mode = alc885_mba21_ch_modes,
		.num_channel_mode = ARRAY_SIZE(alc885_mba21_ch_modes),
		.input_mux = &alc889A_imac91_capture_source,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc885_imac91_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC882_TARGA] = {
		.mixers = { alc882_targa_mixer, alc882_chmode_mixer },
		.init_verbs = { alc882_base_init_verbs, alc882_adc1_init_verbs,
				alc880_gpio3_init_verbs, alc882_targa_verbs},
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.num_adc_nids = ARRAY_SIZE(alc882_adc_nids),
		.adc_nids = alc882_adc_nids,
		.capsrc_nids = alc882_capsrc_nids,
		.num_channel_mode = ARRAY_SIZE(alc882_3ST_6ch_modes),
		.channel_mode = alc882_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc882_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc882_targa_setup,
		.init_hook = alc882_targa_automute,
	},
	[ALC882_ASUS_A7J] = {
		.mixers = { alc882_asus_a7j_mixer, alc882_chmode_mixer },
		.init_verbs = { alc882_base_init_verbs, alc882_adc1_init_verbs,
				alc882_asus_a7j_verbs},
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.num_adc_nids = ARRAY_SIZE(alc882_adc_nids),
		.adc_nids = alc882_adc_nids,
		.capsrc_nids = alc882_capsrc_nids,
		.num_channel_mode = ARRAY_SIZE(alc882_3ST_6ch_modes),
		.channel_mode = alc882_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc882_capture_source,
	},
	[ALC882_ASUS_A7M] = {
		.mixers = { alc882_asus_a7m_mixer, alc882_chmode_mixer },
		.init_verbs = { alc882_base_init_verbs, alc882_adc1_init_verbs,
				alc882_eapd_verbs, alc880_gpio1_init_verbs,
				alc882_asus_a7m_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc882_capture_source,
	},
	[ALC883_3ST_2ch_DIG] = {
		.mixers = { alc883_3ST_2ch_mixer },
		.init_verbs = { alc883_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_3ST_6ch_DIG] = {
		.mixers = { alc883_3ST_6ch_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_3ST_6ch] = {
		.mixers = { alc883_3ST_6ch_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_3ST_6ch_INTEL] = {
		.mixers = { alc883_3ST_6ch_intel_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.slave_dig_outs = alc883_slave_dig_outs,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_intel_modes),
		.channel_mode = alc883_3ST_6ch_intel_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_3stack_6ch_intel,
	},
	[ALC889A_INTEL] = {
		.mixers = { alc885_8ch_intel_mixer, alc883_chmode_mixer },
		.init_verbs = { alc885_init_verbs, alc885_init_input_verbs,
				alc_hp15_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc889_adc_nids),
		.adc_nids = alc889_adc_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.slave_dig_outs = alc883_slave_dig_outs,
		.num_channel_mode = ARRAY_SIZE(alc889_8ch_intel_modes),
		.channel_mode = alc889_8ch_intel_modes,
		.capsrc_nids = alc889_capsrc_nids,
		.input_mux = &alc889_capture_source,
		.setup = alc889_automute_setup,
		.init_hook = alc_hp_automute,
		.unsol_event = alc_sku_unsol_event,
		.need_dac_fix = 1,
	},
	[ALC889_INTEL] = {
		.mixers = { alc885_8ch_intel_mixer, alc883_chmode_mixer },
		.init_verbs = { alc885_init_verbs, alc889_init_input_verbs,
				alc889_eapd_verbs, alc_hp15_unsol_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc889_adc_nids),
		.adc_nids = alc889_adc_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.slave_dig_outs = alc883_slave_dig_outs,
		.num_channel_mode = ARRAY_SIZE(alc889_8ch_intel_modes),
		.channel_mode = alc889_8ch_intel_modes,
		.capsrc_nids = alc889_capsrc_nids,
		.input_mux = &alc889_capture_source,
		.setup = alc889_automute_setup,
		.init_hook = alc889_intel_init_hook,
		.unsol_event = alc_sku_unsol_event,
		.need_dac_fix = 1,
	},
	[ALC883_6ST_DIG] = {
		.mixers = { alc883_base_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_sixstack_modes),
		.channel_mode = alc883_sixstack_modes,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_TARGA_DIG] = {
		.mixers = { alc883_targa_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc880_gpio3_init_verbs,
				alc883_targa_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_targa_unsol_event,
		.setup = alc882_targa_setup,
		.init_hook = alc882_targa_automute,
	},
	[ALC883_TARGA_2ch_DIG] = {
		.mixers = { alc883_targa_2ch_mixer},
		.init_verbs = { alc883_init_verbs, alc880_gpio3_init_verbs,
				alc883_targa_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.adc_nids = alc883_adc_nids_alt,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids_alt),
		.capsrc_nids = alc883_capsrc_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_targa_unsol_event,
		.setup = alc882_targa_setup,
		.init_hook = alc882_targa_automute,
	},
	[ALC883_TARGA_8ch_DIG] = {
		.mixers = { alc883_targa_mixer, alc883_targa_8ch_mixer,
			    alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc880_gpio3_init_verbs,
				alc883_targa_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids_rev),
		.adc_nids = alc883_adc_nids_rev,
		.capsrc_nids = alc883_capsrc_nids_rev,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_4ST_8ch_modes),
		.channel_mode = alc883_4ST_8ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_targa_unsol_event,
		.setup = alc882_targa_setup,
		.init_hook = alc882_targa_automute,
	},
	[ALC883_ACER] = {
		.mixers = { alc883_base_mixer },
		/* On TravelMate laptops, GPIO 0 enables the internal speaker
		 * and the headphone jack.  Turn this on and rely on the
		 * standard mute methods whenever the user wants to turn
		 * these outputs off.
		 */
		.init_verbs = { alc883_init_verbs, alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_ACER_ASPIRE] = {
		.mixers = { alc883_acer_aspire_mixer },
		.init_verbs = { alc883_init_verbs, alc883_acer_eapd_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc883_acer_aspire_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC888_ACER_ASPIRE_4930G] = {
		.mixers = { alc888_acer_aspire_4930g_mixer,
				alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc880_gpio1_init_verbs,
				alc888_acer_aspire_4930g_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids_rev),
		.adc_nids = alc883_adc_nids_rev,
		.capsrc_nids = alc883_capsrc_nids_rev,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.const_channel_count = 6,
		.num_mux_defs =
			ARRAY_SIZE(alc888_2_capture_sources),
		.input_mux = alc888_2_capture_sources,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc888_acer_aspire_4930g_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC888_ACER_ASPIRE_6530G] = {
		.mixers = { alc888_acer_aspire_6530_mixer },
		.init_verbs = { alc883_init_verbs, alc880_gpio1_init_verbs,
				alc888_acer_aspire_6530g_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids_rev),
		.adc_nids = alc883_adc_nids_rev,
		.capsrc_nids = alc883_capsrc_nids_rev,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.num_mux_defs =
			ARRAY_SIZE(alc888_2_capture_sources),
		.input_mux = alc888_acer_aspire_6530_sources,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc888_acer_aspire_6530g_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC888_ACER_ASPIRE_8930G] = {
		.mixers = { alc889_acer_aspire_8930g_mixer,
				alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc880_gpio1_init_verbs,
				alc889_acer_aspire_8930g_verbs,
				alc889_eapd_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc889_adc_nids),
		.adc_nids = alc889_adc_nids,
		.capsrc_nids = alc889_capsrc_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.const_channel_count = 6,
		.num_mux_defs =
			ARRAY_SIZE(alc889_capture_sources),
		.input_mux = alc889_capture_sources,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc889_acer_aspire_8930g_setup,
		.init_hook = alc_hp_automute,
#ifdef CONFIG_SND_HDA_POWER_SAVE
		.power_hook = alc_power_eapd,
#endif
	},
	[ALC888_ACER_ASPIRE_7730G] = {
		.mixers = { alc883_3ST_6ch_mixer,
				alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc880_gpio1_init_verbs,
				alc888_acer_aspire_7730G_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids_rev),
		.adc_nids = alc883_adc_nids_rev,
		.capsrc_nids = alc883_capsrc_nids_rev,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.const_channel_count = 6,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc888_acer_aspire_7730g_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC883_MEDION] = {
		.mixers = { alc883_fivestack_mixer,
			    alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs,
				alc883_medion_eapd_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.adc_nids = alc883_adc_nids_alt,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids_alt),
		.capsrc_nids = alc883_capsrc_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_sixstack_modes),
		.channel_mode = alc883_sixstack_modes,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_MEDION_WIM2160] = {
		.mixers = { alc883_medion_wim2160_mixer },
		.init_verbs = { alc883_init_verbs, alc883_medion_wim2160_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids),
		.adc_nids = alc883_adc_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc883_medion_wim2160_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC883_LAPTOP_EAPD] = {
		.mixers = { alc883_base_mixer },
		.init_verbs = { alc883_init_verbs, alc882_eapd_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_CLEVO_M540R] = {
		.mixers = { alc883_3ST_6ch_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc883_clevo_m540r_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_clevo_modes),
		.channel_mode = alc883_3ST_6ch_clevo_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
		/* This machine has the hardware HP auto-muting, thus
		 * we need no software mute via unsol event
		 */
	},
	[ALC883_CLEVO_M720] = {
		.mixers = { alc883_clevo_m720_mixer },
		.init_verbs = { alc883_init_verbs, alc883_clevo_m720_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_clevo_m720_unsol_event,
		.setup = alc883_clevo_m720_setup,
		.init_hook = alc883_clevo_m720_init_hook,
	},
	[ALC883_LENOVO_101E_2ch] = {
		.mixers = { alc883_lenovo_101e_2ch_mixer},
		.init_verbs = { alc883_init_verbs, alc883_lenovo_101e_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.adc_nids = alc883_adc_nids_alt,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids_alt),
		.capsrc_nids = alc883_capsrc_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_lenovo_101e_capture_source,
		.setup = alc883_lenovo_101e_setup,
		.unsol_event = alc_sku_unsol_event,
		.init_hook = alc_inithook,
	},
	[ALC883_LENOVO_NB0763] = {
		.mixers = { alc883_lenovo_nb0763_mixer },
		.init_verbs = { alc883_init_verbs, alc883_lenovo_nb0763_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_lenovo_nb0763_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc883_lenovo_nb0763_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC888_LENOVO_MS7195_DIG] = {
		.mixers = { alc883_3ST_6ch_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc888_lenovo_ms7195_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc888_lenovo_ms7195_setup,
		.init_hook = alc_inithook,
	},
	[ALC883_HAIER_W66] = {
		.mixers = { alc883_targa_2ch_mixer},
		.init_verbs = { alc883_init_verbs, alc883_haier_w66_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc883_haier_w66_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC888_3ST_HP] = {
		.mixers = { alc883_3ST_6ch_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc888_3st_hp_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc888_3st_hp_modes),
		.channel_mode = alc888_3st_hp_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc888_3st_hp_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC888_6ST_DELL] = {
		.mixers = { alc883_base_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc888_6st_dell_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_sixstack_modes),
		.channel_mode = alc883_sixstack_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc888_6st_dell_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC883_MITAC] = {
		.mixers = { alc883_mitac_mixer },
		.init_verbs = { alc883_init_verbs, alc883_mitac_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc883_mitac_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC883_FUJITSU_PI2515] = {
		.mixers = { alc883_2ch_fujitsu_pi2515_mixer },
		.init_verbs = { alc883_init_verbs,
				alc883_2ch_fujitsu_pi2515_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_fujitsu_pi2515_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc883_2ch_fujitsu_pi2515_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC888_FUJITSU_XA3530] = {
		.mixers = { alc888_base_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs,
			alc888_fujitsu_xa3530_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids_rev),
		.adc_nids = alc883_adc_nids_rev,
		.capsrc_nids = alc883_capsrc_nids_rev,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc888_4ST_8ch_intel_modes),
		.channel_mode = alc888_4ST_8ch_intel_modes,
		.num_mux_defs =
			ARRAY_SIZE(alc888_2_capture_sources),
		.input_mux = alc888_2_capture_sources,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc888_fujitsu_xa3530_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC888_LENOVO_SKY] = {
		.mixers = { alc888_lenovo_sky_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc888_lenovo_sky_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_sixstack_modes),
		.channel_mode = alc883_sixstack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_lenovo_sky_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc888_lenovo_sky_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC888_ASUS_M90V] = {
		.mixers = { alc883_3ST_6ch_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc888_asus_m90v_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_fujitsu_pi2515_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc883_mode2_setup,
		.init_hook = alc_inithook,
	},
	[ALC888_ASUS_EEE1601] = {
		.mixers = { alc883_asus_eee1601_mixer },
		.cap_mixer = alc883_asus_eee1601_cap_mixer,
		.init_verbs = { alc883_init_verbs, alc888_asus_eee1601_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_asus_eee1601_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.init_hook = alc883_eee1601_inithook,
	},
	[ALC1200_ASUS_P5Q] = {
		.mixers = { alc883_base_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC1200_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.slave_dig_outs = alc1200_slave_dig_outs,
		.num_channel_mode = ARRAY_SIZE(alc883_sixstack_modes),
		.channel_mode = alc883_sixstack_modes,
		.input_mux = &alc883_capture_source,
	},
	[ALC889A_MB31] = {
		.mixers = { alc889A_mb31_mixer, alc883_chmode_mixer},
		.init_verbs = { alc883_init_verbs, alc889A_mb31_verbs,
			alc880_gpio1_init_verbs },
		.adc_nids = alc883_adc_nids,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids),
		.capsrc_nids = alc883_capsrc_nids,
		.dac_nids = alc883_dac_nids,
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.channel_mode = alc889A_mb31_6ch_modes,
		.num_channel_mode = ARRAY_SIZE(alc889A_mb31_6ch_modes),
		.input_mux = &alc889A_mb31_capture_source,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.unsol_event = alc889A_mb31_unsol_event,
		.init_hook = alc889A_mb31_automute,
	},
	[ALC883_SONY_VAIO_TT] = {
		.mixers = { alc883_vaiott_mixer },
		.init_verbs = { alc883_init_verbs, alc883_vaiott_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc883_vaiott_setup,
		.init_hook = alc_hp_automute,
	},
};


