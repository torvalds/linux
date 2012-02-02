/*
 * ALC880 quirk models
 * included by patch_realtek.c
 */

/* ALC880 board config type */
enum {
	ALC880_AUTO,
	ALC880_3ST,
	ALC880_3ST_DIG,
	ALC880_5ST,
	ALC880_5ST_DIG,
	ALC880_W810,
	ALC880_Z71V,
	ALC880_6ST,
	ALC880_6ST_DIG,
	ALC880_F1734,
	ALC880_ASUS,
	ALC880_ASUS_DIG,
	ALC880_ASUS_W1V,
	ALC880_ASUS_DIG2,
	ALC880_FUJITSU,
	ALC880_UNIWILL_DIG,
	ALC880_UNIWILL,
	ALC880_UNIWILL_P53,
	ALC880_CLEVO,
	ALC880_TCL_S700,
	ALC880_LG,
#ifdef CONFIG_SND_DEBUG
	ALC880_TEST,
#endif
	ALC880_MODEL_LAST /* last tag */
};

/*
 * ALC880 3-stack model
 *
 * DAC: Front = 0x02 (0x0c), Surr = 0x05 (0x0f), CLFE = 0x04 (0x0e)
 * Pin assignment: Front = 0x14, Line-In/Surr = 0x1a, Mic/CLFE = 0x18,
 *                 F-Mic = 0x1b, HP = 0x19
 */

static const hda_nid_t alc880_dac_nids[4] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x05, 0x04, 0x03
};

static const hda_nid_t alc880_adc_nids[3] = {
	/* ADC0-2 */
	0x07, 0x08, 0x09,
};

/* The datasheet says the node 0x07 is connected from inputs,
 * but it shows zero connection in the real implementation on some devices.
 * Note: this is a 915GAV bug, fixed on 915GLV
 */
static const hda_nid_t alc880_adc_nids_alt[2] = {
	/* ADC1-2 */
	0x08, 0x09,
};

#define ALC880_DIGOUT_NID	0x06
#define ALC880_DIGIN_NID	0x0a
#define ALC880_PIN_CD_NID	0x1c

static const struct hda_input_mux alc880_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x3 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

/* channel source setting (2/6 channel selection for 3-stack) */
/* 2ch mode */
static const struct hda_verb alc880_threestack_ch2_init[] = {
	/* set line-in to input, mute it */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	/* set mic-in to input vref 80%, mute it */
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/* 6ch mode */
static const struct hda_verb alc880_threestack_ch6_init[] = {
	/* set line-in to output, unmute it */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	/* set mic-in to output, unmute it */
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ } /* end */
};

static const struct hda_channel_mode alc880_threestack_modes[2] = {
	{ 2, alc880_threestack_ch2_init },
	{ 6, alc880_threestack_ch6_init },
};

static const struct snd_kcontrol_new alc880_three_stack_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x19, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

/*
 * ALC880 5-stack model
 *
 * DAC: Front = 0x02 (0x0c), Surr = 0x05 (0x0f), CLFE = 0x04 (0x0d),
 *      Side = 0x02 (0xd)
 * Pin assignment: Front = 0x14, Surr = 0x17, CLFE = 0x16
 *                 Line-In/Side = 0x1a, Mic = 0x18, F-Mic = 0x1b, HP = 0x19
 */

/* additional mixers to alc880_three_stack_mixer */
static const struct snd_kcontrol_new alc880_five_stack_mixer[] = {
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0d, 2, HDA_INPUT),
	{ } /* end */
};

/* channel source setting (6/8 channel selection for 5-stack) */
/* 6ch mode */
static const struct hda_verb alc880_fivestack_ch6_init[] = {
	/* set line-in to input, mute it */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/* 8ch mode */
static const struct hda_verb alc880_fivestack_ch8_init[] = {
	/* set line-in to output, unmute it */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ } /* end */
};

static const struct hda_channel_mode alc880_fivestack_modes[2] = {
	{ 6, alc880_fivestack_ch6_init },
	{ 8, alc880_fivestack_ch8_init },
};


/*
 * ALC880 6-stack model
 *
 * DAC: Front = 0x02 (0x0c), Surr = 0x03 (0x0d), CLFE = 0x04 (0x0e),
 *      Side = 0x05 (0x0f)
 * Pin assignment: Front = 0x14, Surr = 0x15, CLFE = 0x16, Side = 0x17,
 *   Mic = 0x18, F-Mic = 0x19, Line = 0x1a, HP = 0x1b
 */

static const hda_nid_t alc880_6st_dac_nids[4] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x03, 0x04, 0x05
};

static const struct hda_input_mux alc880_6stack_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

/* fixed 8-channels */
static const struct hda_channel_mode alc880_sixstack_modes[1] = {
	{ 8, NULL },
};

static const struct snd_kcontrol_new alc880_six_stack_mixer[] = {
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
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};


/*
 * ALC880 W810 model
 *
 * W810 has rear IO for:
 * Front (DAC 02)
 * Surround (DAC 03)
 * Center/LFE (DAC 04)
 * Digital out (06)
 *
 * The system also has a pair of internal speakers, and a headphone jack.
 * These are both connected to Line2 on the codec, hence to DAC 02.
 *
 * There is a variable resistor to control the speaker or headphone
 * volume. This is a hardware-only device without a software API.
 *
 * Plugging headphones in will disable the internal speakers. This is
 * implemented in hardware, not via the driver using jack sense. In
 * a similar fashion, plugging into the rear socket marked "front" will
 * disable both the speakers and headphones.
 *
 * For input, there's a microphone jack, and an "audio in" jack.
 * These may not do anything useful with this driver yet, because I
 * haven't setup any initialization verbs for these yet...
 */

static const hda_nid_t alc880_w810_dac_nids[3] = {
	/* front, rear/surround, clfe */
	0x02, 0x03, 0x04
};

/* fixed 6 channels */
static const struct hda_channel_mode alc880_w810_modes[1] = {
	{ 6, NULL }
};

/* Pin assignment: Front = 0x14, Surr = 0x15, CLFE = 0x16, HP = 0x1b */
static const struct snd_kcontrol_new alc880_w810_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	{ } /* end */
};


/*
 * Z710V model
 *
 * DAC: Front = 0x02 (0x0c), HP = 0x03 (0x0d)
 * Pin assignment: Front = 0x14, HP = 0x15, Mic = 0x18, Mic2 = 0x19(?),
 *                 Line = 0x1a
 */

static const hda_nid_t alc880_z71v_dac_nids[1] = {
	0x02
};
#define ALC880_Z71V_HP_DAC	0x03

/* fixed 2 channels */
static const struct hda_channel_mode alc880_2_jack_modes[1] = {
	{ 2, NULL }
};

static const struct snd_kcontrol_new alc880_z71v_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};


/*
 * ALC880 F1734 model
 *
 * DAC: HP = 0x02 (0x0c), Front = 0x03 (0x0d)
 * Pin assignment: HP = 0x14, Front = 0x15, Mic = 0x18
 */

static const hda_nid_t alc880_f1734_dac_nids[1] = {
	0x03
};
#define ALC880_F1734_HP_DAC	0x02

static const struct snd_kcontrol_new alc880_f1734_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct hda_input_mux alc880_f1734_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x1 },
		{ "CD", 0x4 },
	},
};


/*
 * ALC880 ASUS model
 *
 * DAC: HP/Front = 0x02 (0x0c), Surr = 0x03 (0x0d), CLFE = 0x04 (0x0e)
 * Pin assignment: HP/Front = 0x14, Surr = 0x15, CLFE = 0x16,
 *  Mic = 0x18, Line = 0x1a
 */

#define alc880_asus_dac_nids	alc880_w810_dac_nids	/* identical with w810 */
#define alc880_asus_modes	alc880_threestack_modes	/* 2/6 channel mode */

static const struct snd_kcontrol_new alc880_asus_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
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
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

/*
 * ALC880 ASUS W1V model
 *
 * DAC: HP/Front = 0x02 (0x0c), Surr = 0x03 (0x0d), CLFE = 0x04 (0x0e)
 * Pin assignment: HP/Front = 0x14, Surr = 0x15, CLFE = 0x16,
 *  Mic = 0x18, Line = 0x1a, Line2 = 0x1b
 */

/* additional mixers to alc880_asus_mixer */
static const struct snd_kcontrol_new alc880_asus_w1v_mixer[] = {
	HDA_CODEC_VOLUME("Line2 Playback Volume", 0x0b, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("Line2 Playback Switch", 0x0b, 0x03, HDA_INPUT),
	{ } /* end */
};

/* TCL S700 */
static const struct snd_kcontrol_new alc880_tcl_s700_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0B, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0B, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0B, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0B, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{ } /* end */
};

/* Uniwill */
static const struct snd_kcontrol_new alc880_uniwill_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static const struct snd_kcontrol_new alc880_fujitsu_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc880_uniwill_p53_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

/*
 * initialize the codec volumes, etc
 */

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static const struct hda_verb alc880_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for front
	 * panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)},

	/*
	 * Set up output mixers (0x0c - 0x0f)
	 */
	/* set vol=0 to output mixers */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	{ }
};

/*
 * 3-stack pin configuration:
 * front = 0x14, mic/clfe = 0x18, HP = 0x19, line/surr = 0x1a, f-mic = 0x1b
 */
static const struct hda_verb alc880_pin_3stack_init_verbs[] = {
	/*
	 * preset connection lists of input pins
	 * 0 = front, 1 = rear_surr, 2 = CLFE, 3 = surround
	 */
	{0x10, AC_VERB_SET_CONNECT_SEL, 0x02}, /* mic/clfe */
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
	{0x12, AC_VERB_SET_CONNECT_SEL, 0x03}, /* line/surround */

	/*
	 * Set pin mode and muting
	 */
	/* set front pin widgets 0x14 for output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Mic2 (as headphone out) for HP output */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Line In pin widget for input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line2 (as front mic) pin widget for input and vref at 80% */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{ }
};

/*
 * 5-stack pin configuration:
 * front = 0x14, surround = 0x17, clfe = 0x16, mic = 0x18, HP = 0x19,
 * line-in/side = 0x1a, f-mic = 0x1b
 */
static const struct hda_verb alc880_pin_5stack_init_verbs[] = {
	/*
	 * preset connection lists of input pins
	 * 0 = front, 1 = rear_surr, 2 = CLFE, 3 = surround
	 */
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
	{0x12, AC_VERB_SET_CONNECT_SEL, 0x01}, /* line/side */

	/*
	 * Set pin mode and muting
	 */
	/* set pin widgets 0x14-0x17 for output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* unmute pins for output (no gain on this amp) */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Mic2 (as headphone out) for HP output */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Line In pin widget for input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line2 (as front mic) pin widget for input and vref at 80% */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{ }
};

/*
 * W810 pin configuration:
 * front = 0x14, surround = 0x15, clfe = 0x16, HP = 0x1b
 */
static const struct hda_verb alc880_pin_w810_init_verbs[] = {
	/* hphone/speaker input selector: front DAC */
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x0},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{ }
};

/*
 * Z71V pin configuration:
 * Speaker-out = 0x14, HP = 0x15, Mic = 0x18, Line-in = 0x1a, Mic2 = 0x1b (?)
 */
static const struct hda_verb alc880_pin_z71v_init_verbs[] = {
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{ }
};

/*
 * 6-stack pin configuration:
 * front = 0x14, surr = 0x15, clfe = 0x16, side = 0x17, mic = 0x18,
 * f-mic = 0x19, line = 0x1a, HP = 0x1b
 */
static const struct hda_verb alc880_pin_6stack_init_verbs[] = {
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{ }
};

/*
 * Uniwill pin configuration:
 * HP = 0x14, InternalSpeaker = 0x15, mic = 0x18, internal mic = 0x19,
 * line = 0x1a
 */
static const struct hda_verb alc880_uniwill_init_verbs[] = {
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* {0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP}, */
	/* {0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_HP_EVENT},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_MIC_EVENT},

	{ }
};

/*
* Uniwill P53
* HP = 0x14, InternalSpeaker = 0x15, mic = 0x19,
 */
static const struct hda_verb alc880_uniwill_p53_init_verbs[] = {
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_HP_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_DCVOL_EVENT},

	{ }
};

static const struct hda_verb alc880_beep_init_verbs[] = {
	{ 0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(5) },
	{ }
};

static void alc880_uniwill_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x16;
	alc_simple_setup_automute(spec, ALC_AUTOMUTE_AMP);
}

static void alc880_uniwill_init_hook(struct hda_codec *codec)
{
	alc_hp_automute(codec);
	alc88x_simple_mic_automute(codec);
}

static void alc880_uniwill_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	/* Looks like the unsol event is incompatible with the standard
	 * definition.  4bit tag is placed at 28 bit!
	 */
	res >>= 28;
	switch (res) {
	case ALC_MIC_EVENT:
		alc88x_simple_mic_automute(codec);
		break;
	default:
		alc_exec_unsol_event(codec, res);
		break;
	}
}

static void alc880_unsol_event(struct hda_codec *codec, unsigned int res)
{
	alc_exec_unsol_event(codec, res >> 28);
}

static void alc880_uniwill_p53_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x15;
	alc_simple_setup_automute(spec, ALC_AUTOMUTE_AMP);
}

static void alc880_uniwill_p53_dcvol_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x21, 0,
				     AC_VERB_GET_VOLUME_KNOB_CONTROL, 0);
	present &= HDA_AMP_VOLMASK;
	snd_hda_codec_amp_stereo(codec, 0x0c, HDA_OUTPUT, 0,
				 HDA_AMP_VOLMASK, present);
	snd_hda_codec_amp_stereo(codec, 0x0d, HDA_OUTPUT, 0,
				 HDA_AMP_VOLMASK, present);
}

static void alc880_uniwill_p53_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	/* Looks like the unsol event is incompatible with the standard
	 * definition.  4bit tag is placed at 28 bit!
	 */
	res >>= 28;
	if (res == ALC_DCVOL_EVENT)
		alc880_uniwill_p53_dcvol_automute(codec);
	else
		alc_exec_unsol_event(codec, res);
}

/*
 * F1734 pin configuration:
 * HP = 0x14, speaker-out = 0x15, mic = 0x18
 */
static const struct hda_verb alc880_pin_f1734_init_verbs[] = {
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x10, AC_VERB_SET_CONNECT_SEL, 0x02},
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x12, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF50},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC_HP_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC_DCVOL_EVENT},

	{ }
};

/*
 * ASUS pin configuration:
 * HP/front = 0x14, surr = 0x15, clfe = 0x16, mic = 0x18, line = 0x1a
 */
static const struct hda_verb alc880_pin_asus_init_verbs[] = {
	{0x10, AC_VERB_SET_CONNECT_SEL, 0x02},
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x12, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{ }
};

/* Enable GPIO mask and set output */
#define alc880_gpio1_init_verbs	alc_gpio1_init_verbs
#define alc880_gpio2_init_verbs	alc_gpio2_init_verbs
#define alc880_gpio3_init_verbs	alc_gpio3_init_verbs

/* Clevo m520g init */
static const struct hda_verb alc880_pin_clevo_init_verbs[] = {
	/* headphone output */
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x01},
	/* line-out */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Line-in */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* CD */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Mic1 (rear panel) */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Mic2 (front panel) */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* headphone */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
        /* change to EAPD mode */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF,  0x3060},

	{ }
};

static const struct hda_verb alc880_pin_tcl_S700_init_verbs[] = {
	/* change to EAPD mode */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF,  0x3060},

	/* Headphone output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Front output*/
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Line In pin widget for input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},

	/* change to EAPD mode */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF,  0x3070},

	{ }
};

/*
 * LG m1 express dual
 *
 * Pin assignment:
 *   Rear Line-In/Out (blue): 0x14
 *   Build-in Mic-In: 0x15
 *   Speaker-out: 0x17
 *   HP-Out (green): 0x1b
 *   Mic-In/Out (red): 0x19
 *   SPDIF-Out: 0x1e
 */

/* To make 5.1 output working (green=Front, blue=Surr, red=CLFE) */
static const hda_nid_t alc880_lg_dac_nids[3] = {
	0x05, 0x02, 0x03
};

/* seems analog CD is not working */
static const struct hda_input_mux alc880_lg_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x1 },
		{ "Line", 0x5 },
		{ "Internal Mic", 0x6 },
	},
};

/* 2,4,6 channel modes */
static const struct hda_verb alc880_lg_ch2_init[] = {
	/* set line-in and mic-in to input */
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ }
};

static const struct hda_verb alc880_lg_ch4_init[] = {
	/* set line-in to out and mic-in to input */
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ }
};

static const struct hda_verb alc880_lg_ch6_init[] = {
	/* set line-in and mic-in to output */
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{ }
};

static const struct hda_channel_mode alc880_lg_ch_modes[3] = {
	{ 2, alc880_lg_ch2_init },
	{ 4, alc880_lg_ch4_init },
	{ 6, alc880_lg_ch6_init },
};

static const struct snd_kcontrol_new alc880_lg_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0d, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0d, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0d, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0d, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x06, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x06, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x07, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x07, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static const struct hda_verb alc880_lg_init_verbs[] = {
	/* set capture source to mic-in */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* mute all amp mixer inputs */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(5)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)},
	/* line-in to input */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* built-in mic */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* speaker-out */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* mic-in to input */
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* HP-out */
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x03},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* jack sense */
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_HP_EVENT},
	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc880_lg_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x17;
	alc_simple_setup_automute(spec, ALC_AUTOMUTE_AMP);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static const struct hda_amp_list alc880_lg_loopbacks[] = {
	{ 0x0b, HDA_INPUT, 1 },
	{ 0x0b, HDA_INPUT, 6 },
	{ 0x0b, HDA_INPUT, 7 },
	{ } /* end */
};
#endif

/*
 * Test configuration for debugging
 *
 * Almost all inputs/outputs are enabled.  I/O pins can be configured via
 * enum controls.
 */
#ifdef CONFIG_SND_DEBUG
static const hda_nid_t alc880_test_dac_nids[4] = {
	0x02, 0x03, 0x04, 0x05
};

static const struct hda_input_mux alc880_test_capture_source = {
	.num_items = 7,
	.items = {
		{ "In-1", 0x0 },
		{ "In-2", 0x1 },
		{ "In-3", 0x2 },
		{ "In-4", 0x3 },
		{ "CD", 0x4 },
		{ "Front", 0x5 },
		{ "Surround", 0x6 },
	},
};

static const struct hda_channel_mode alc880_test_modes[4] = {
	{ 2, NULL },
	{ 4, NULL },
	{ 6, NULL },
	{ 8, NULL },
};

static int alc_test_pin_ctl_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = {
		"N/A", "Line Out", "HP Out",
		"In Hi-Z", "In 50%", "In Grd", "In 80%", "In 100%"
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 8;
	if (uinfo->value.enumerated.item >= 8)
		uinfo->value.enumerated.item = 7;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int alc_test_pin_ctl_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = (hda_nid_t)kcontrol->private_value;
	unsigned int pin_ctl, item = 0;

	pin_ctl = snd_hda_codec_read(codec, nid, 0,
				     AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
	if (pin_ctl & AC_PINCTL_OUT_EN) {
		if (pin_ctl & AC_PINCTL_HP_EN)
			item = 2;
		else
			item = 1;
	} else if (pin_ctl & AC_PINCTL_IN_EN) {
		switch (pin_ctl & AC_PINCTL_VREFEN) {
		case AC_PINCTL_VREF_HIZ: item = 3; break;
		case AC_PINCTL_VREF_50:  item = 4; break;
		case AC_PINCTL_VREF_GRD: item = 5; break;
		case AC_PINCTL_VREF_80:  item = 6; break;
		case AC_PINCTL_VREF_100: item = 7; break;
		}
	}
	ucontrol->value.enumerated.item[0] = item;
	return 0;
}

static int alc_test_pin_ctl_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = (hda_nid_t)kcontrol->private_value;
	static const unsigned int ctls[] = {
		0, AC_PINCTL_OUT_EN, AC_PINCTL_OUT_EN | AC_PINCTL_HP_EN,
		AC_PINCTL_IN_EN | AC_PINCTL_VREF_HIZ,
		AC_PINCTL_IN_EN | AC_PINCTL_VREF_50,
		AC_PINCTL_IN_EN | AC_PINCTL_VREF_GRD,
		AC_PINCTL_IN_EN | AC_PINCTL_VREF_80,
		AC_PINCTL_IN_EN | AC_PINCTL_VREF_100,
	};
	unsigned int old_ctl, new_ctl;

	old_ctl = snd_hda_codec_read(codec, nid, 0,
				     AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
	new_ctl = ctls[ucontrol->value.enumerated.item[0]];
	if (old_ctl != new_ctl) {
		int val;
		snd_hda_codec_write_cache(codec, nid, 0,
					  AC_VERB_SET_PIN_WIDGET_CONTROL,
					  new_ctl);
		val = ucontrol->value.enumerated.item[0] >= 3 ?
			HDA_AMP_MUTE : 0;
		snd_hda_codec_amp_stereo(codec, nid, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, val);
		return 1;
	}
	return 0;
}

static int alc_test_pin_src_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = {
		"Front", "Surround", "CLFE", "Side"
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item >= 4)
		uinfo->value.enumerated.item = 3;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int alc_test_pin_src_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = (hda_nid_t)kcontrol->private_value;
	unsigned int sel;

	sel = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_CONNECT_SEL, 0);
	ucontrol->value.enumerated.item[0] = sel & 3;
	return 0;
}

static int alc_test_pin_src_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = (hda_nid_t)kcontrol->private_value;
	unsigned int sel;

	sel = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_CONNECT_SEL, 0) & 3;
	if (ucontrol->value.enumerated.item[0] != sel) {
		sel = ucontrol->value.enumerated.item[0] & 3;
		snd_hda_codec_write_cache(codec, nid, 0,
					  AC_VERB_SET_CONNECT_SEL, sel);
		return 1;
	}
	return 0;
}

#define PIN_CTL_TEST(xname,nid) {			\
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,	\
			.name = xname,		       \
			.subdevice = HDA_SUBDEV_NID_FLAG | nid, \
			.info = alc_test_pin_ctl_info, \
			.get = alc_test_pin_ctl_get,   \
			.put = alc_test_pin_ctl_put,   \
			.private_value = nid	       \
			}

#define PIN_SRC_TEST(xname,nid) {			\
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,	\
			.name = xname,		       \
			.subdevice = HDA_SUBDEV_NID_FLAG | nid, \
			.info = alc_test_pin_src_info, \
			.get = alc_test_pin_src_get,   \
			.put = alc_test_pin_src_put,   \
			.private_value = nid	       \
			}

static const struct snd_kcontrol_new alc880_test_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CLFE Playback Volume", 0x0e, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_BIND_MUTE("CLFE Playback Switch", 0x0e, 2, HDA_INPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0f, 2, HDA_INPUT),
	PIN_CTL_TEST("Front Pin Mode", 0x14),
	PIN_CTL_TEST("Surround Pin Mode", 0x15),
	PIN_CTL_TEST("CLFE Pin Mode", 0x16),
	PIN_CTL_TEST("Side Pin Mode", 0x17),
	PIN_CTL_TEST("In-1 Pin Mode", 0x18),
	PIN_CTL_TEST("In-2 Pin Mode", 0x19),
	PIN_CTL_TEST("In-3 Pin Mode", 0x1a),
	PIN_CTL_TEST("In-4 Pin Mode", 0x1b),
	PIN_SRC_TEST("In-1 Pin Source", 0x18),
	PIN_SRC_TEST("In-2 Pin Source", 0x19),
	PIN_SRC_TEST("In-3 Pin Source", 0x1a),
	PIN_SRC_TEST("In-4 Pin Source", 0x1b),
	HDA_CODEC_VOLUME("In-1 Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("In-1 Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("In-2 Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("In-2 Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("In-3 Playback Volume", 0x0b, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("In-3 Playback Switch", 0x0b, 0x2, HDA_INPUT),
	HDA_CODEC_VOLUME("In-4 Playback Volume", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("In-4 Playback Switch", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x4, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static const struct hda_verb alc880_test_init_verbs[] = {
	/* Unmute inputs of 0x0c - 0x0f */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* Vol output for 0x0c-0x0f */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* Set output pins 0x14-0x17 */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* Unmute output pins 0x14-0x17 */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Set input pins 0x18-0x1c */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	/* Mute input pins 0x18-0x1b */
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* ADC set up */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Analog input/passthru */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{ }
};
#endif

/*
 */

static const char * const alc880_models[ALC880_MODEL_LAST] = {
	[ALC880_3ST]		= "3stack",
	[ALC880_TCL_S700]	= "tcl",
	[ALC880_3ST_DIG]	= "3stack-digout",
	[ALC880_CLEVO]		= "clevo",
	[ALC880_5ST]		= "5stack",
	[ALC880_5ST_DIG]	= "5stack-digout",
	[ALC880_W810]		= "w810",
	[ALC880_Z71V]		= "z71v",
	[ALC880_6ST]		= "6stack",
	[ALC880_6ST_DIG]	= "6stack-digout",
	[ALC880_ASUS]		= "asus",
	[ALC880_ASUS_W1V]	= "asus-w1v",
	[ALC880_ASUS_DIG]	= "asus-dig",
	[ALC880_ASUS_DIG2]	= "asus-dig2",
	[ALC880_UNIWILL_DIG]	= "uniwill",
	[ALC880_UNIWILL_P53]	= "uniwill-p53",
	[ALC880_FUJITSU]	= "fujitsu",
	[ALC880_F1734]		= "F1734",
	[ALC880_LG]		= "lg",
#ifdef CONFIG_SND_DEBUG
	[ALC880_TEST]		= "test",
#endif
	[ALC880_AUTO]		= "auto",
};

static const struct snd_pci_quirk alc880_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0x0f69, "Coeus G610P", ALC880_W810),
	SND_PCI_QUIRK(0x1019, 0xa880, "ECS", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x1019, 0xa884, "Acer APFV", ALC880_6ST),
	SND_PCI_QUIRK(0x1025, 0x0070, "ULI", ALC880_3ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0077, "ULI", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0078, "ULI", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0087, "ULI", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0xe309, "ULI", ALC880_3ST_DIG),
	SND_PCI_QUIRK(0x1025, 0xe310, "ULI", ALC880_3ST),
	SND_PCI_QUIRK(0x1039, 0x1234, NULL, ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1043, 0x10b3, "ASUS W1V", ALC880_ASUS_W1V),
	SND_PCI_QUIRK(0x1043, 0x10c2, "ASUS W6A", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x10c3, "ASUS Wxx", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x1113, "ASUS", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x1123, "ASUS", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x1173, "ASUS", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x1964, "ASUS Z71V", ALC880_Z71V),
	/* SND_PCI_QUIRK(0x1043, 0x1964, "ASUS", ALC880_ASUS_DIG), */
	SND_PCI_QUIRK(0x1043, 0x1973, "ASUS", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x19b3, "ASUS", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x814e, "ASUS P5GD1 w/SPDIF", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1043, 0x8181, "ASUS P4GPL", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x8196, "ASUS P5GD1", ALC880_6ST),
	SND_PCI_QUIRK(0x1043, 0x81b4, "ASUS", ALC880_6ST),
	SND_PCI_QUIRK_VENDOR(0x1043, "ASUS", ALC880_ASUS), /* default ASUS */
	SND_PCI_QUIRK(0x104d, 0x81a0, "Sony", ALC880_3ST),
	SND_PCI_QUIRK(0x104d, 0x81d6, "Sony", ALC880_3ST),
	SND_PCI_QUIRK(0x107b, 0x3032, "Gateway", ALC880_5ST),
	SND_PCI_QUIRK(0x107b, 0x3033, "Gateway", ALC880_5ST),
	SND_PCI_QUIRK(0x107b, 0x4039, "Gateway", ALC880_5ST),
	SND_PCI_QUIRK(0x1297, 0xc790, "Shuttle ST20G5", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1458, 0xa102, "Gigabyte K8", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x1150, "MSI", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1509, 0x925d, "FIC P4M", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1558, 0x0520, "Clevo m520G", ALC880_CLEVO),
	SND_PCI_QUIRK(0x1558, 0x0660, "Clevo m655n", ALC880_CLEVO),
	SND_PCI_QUIRK(0x1558, 0x5401, "ASUS", ALC880_ASUS_DIG2),
	SND_PCI_QUIRK(0x1565, 0x8202, "Biostar", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x1584, 0x9050, "Uniwill", ALC880_UNIWILL_DIG),
	SND_PCI_QUIRK(0x1584, 0x9054, "Uniwill", ALC880_F1734),
	SND_PCI_QUIRK(0x1584, 0x9070, "Uniwill", ALC880_UNIWILL),
	SND_PCI_QUIRK(0x1584, 0x9077, "Uniwill P53", ALC880_UNIWILL_P53),
	SND_PCI_QUIRK(0x161f, 0x203d, "W810", ALC880_W810),
	SND_PCI_QUIRK(0x1695, 0x400d, "EPoX", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x1695, 0x4012, "EPox EP-5LDA", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x1734, 0x107c, "FSC F1734", ALC880_F1734),
	SND_PCI_QUIRK(0x1734, 0x1094, "FSC Amilo M1451G", ALC880_FUJITSU),
	SND_PCI_QUIRK(0x1734, 0x10ac, "FSC AMILO Xi 1526", ALC880_F1734),
	SND_PCI_QUIRK(0x1734, 0x10b0, "Fujitsu", ALC880_FUJITSU),
	SND_PCI_QUIRK(0x1854, 0x003b, "LG", ALC880_LG),
	SND_PCI_QUIRK(0x1854, 0x005f, "LG P1 Express", ALC880_LG),
	SND_PCI_QUIRK(0x1854, 0x0068, "LG w1", ALC880_LG),
	SND_PCI_QUIRK(0x19db, 0x4188, "TCL S700", ALC880_TCL_S700),
	SND_PCI_QUIRK(0x2668, 0x8086, NULL, ALC880_6ST_DIG), /* broken BIOS */
	SND_PCI_QUIRK(0x8086, 0x2668, NULL, ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xa100, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd400, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd401, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd402, "Intel mobo", ALC880_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe224, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe305, "Intel mobo", ALC880_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe308, "Intel mobo", ALC880_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe400, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe401, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe402, "Intel mobo", ALC880_5ST_DIG),
	/* default Intel */
	SND_PCI_QUIRK_VENDOR(0x8086, "Intel mobo", ALC880_3ST),
	SND_PCI_QUIRK(0xa0a0, 0x0560, "AOpen i915GMm-HFS", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0xe803, 0x1019, NULL, ALC880_6ST_DIG),
	{}
};

/*
 * ALC880 codec presets
 */
static const struct alc_config_preset alc880_presets[] = {
	[ALC880_3ST] = {
		.mixers = { alc880_three_stack_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_3stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_3ST_DIG] = {
		.mixers = { alc880_three_stack_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_3stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_TCL_S700] = {
		.mixers = { alc880_tcl_s700_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_tcl_S700_init_verbs,
				alc880_gpio2_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.adc_nids = alc880_adc_nids_alt, /* FIXME: correct? */
		.num_adc_nids = 1, /* single ADC */
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc880_2_jack_modes),
		.channel_mode = alc880_2_jack_modes,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_5ST] = {
		.mixers = { alc880_three_stack_mixer,
			    alc880_five_stack_mixer},
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_5stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_fivestack_modes),
		.channel_mode = alc880_fivestack_modes,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_5ST_DIG] = {
		.mixers = { alc880_three_stack_mixer,
			    alc880_five_stack_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_5stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_fivestack_modes),
		.channel_mode = alc880_fivestack_modes,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_6ST] = {
		.mixers = { alc880_six_stack_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_6stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_6st_dac_nids),
		.dac_nids = alc880_6st_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_sixstack_modes),
		.channel_mode = alc880_sixstack_modes,
		.input_mux = &alc880_6stack_capture_source,
	},
	[ALC880_6ST_DIG] = {
		.mixers = { alc880_six_stack_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_6stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_6st_dac_nids),
		.dac_nids = alc880_6st_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_sixstack_modes),
		.channel_mode = alc880_sixstack_modes,
		.input_mux = &alc880_6stack_capture_source,
	},
	[ALC880_W810] = {
		.mixers = { alc880_w810_base_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_w810_init_verbs,
				alc880_gpio2_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_w810_dac_nids),
		.dac_nids = alc880_w810_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_w810_modes),
		.channel_mode = alc880_w810_modes,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_Z71V] = {
		.mixers = { alc880_z71v_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_z71v_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_z71v_dac_nids),
		.dac_nids = alc880_z71v_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc880_2_jack_modes),
		.channel_mode = alc880_2_jack_modes,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_F1734] = {
		.mixers = { alc880_f1734_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_f1734_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_f1734_dac_nids),
		.dac_nids = alc880_f1734_dac_nids,
		.hp_nid = 0x02,
		.num_channel_mode = ARRAY_SIZE(alc880_2_jack_modes),
		.channel_mode = alc880_2_jack_modes,
		.input_mux = &alc880_f1734_capture_source,
		.unsol_event = alc880_uniwill_p53_unsol_event,
		.setup = alc880_uniwill_p53_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC880_ASUS] = {
		.mixers = { alc880_asus_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_asus_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_asus_modes),
		.channel_mode = alc880_asus_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_ASUS_DIG] = {
		.mixers = { alc880_asus_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_asus_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_asus_modes),
		.channel_mode = alc880_asus_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_ASUS_DIG2] = {
		.mixers = { alc880_asus_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_asus_init_verbs,
				alc880_gpio2_init_verbs }, /* use GPIO2 */
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_asus_modes),
		.channel_mode = alc880_asus_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_ASUS_W1V] = {
		.mixers = { alc880_asus_mixer, alc880_asus_w1v_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_asus_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_asus_modes),
		.channel_mode = alc880_asus_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_UNIWILL_DIG] = {
		.mixers = { alc880_asus_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_asus_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_asus_modes),
		.channel_mode = alc880_asus_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_UNIWILL] = {
		.mixers = { alc880_uniwill_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_uniwill_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
		.unsol_event = alc880_uniwill_unsol_event,
		.setup = alc880_uniwill_setup,
		.init_hook = alc880_uniwill_init_hook,
	},
	[ALC880_UNIWILL_P53] = {
		.mixers = { alc880_uniwill_p53_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_uniwill_p53_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_w810_modes),
		.channel_mode = alc880_threestack_modes,
		.input_mux = &alc880_capture_source,
		.unsol_event = alc880_uniwill_p53_unsol_event,
		.setup = alc880_uniwill_p53_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC880_FUJITSU] = {
		.mixers = { alc880_fujitsu_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_uniwill_p53_init_verbs,
	       			alc880_beep_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_2_jack_modes),
		.channel_mode = alc880_2_jack_modes,
		.input_mux = &alc880_capture_source,
		.unsol_event = alc880_uniwill_p53_unsol_event,
		.setup = alc880_uniwill_p53_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC880_CLEVO] = {
		.mixers = { alc880_three_stack_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_clevo_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_LG] = {
		.mixers = { alc880_lg_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_lg_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_lg_dac_nids),
		.dac_nids = alc880_lg_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_lg_ch_modes),
		.channel_mode = alc880_lg_ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_lg_capture_source,
		.unsol_event = alc880_unsol_event,
		.setup = alc880_lg_setup,
		.init_hook = alc_hp_automute,
#ifdef CONFIG_SND_HDA_POWER_SAVE
		.loopbacks = alc880_lg_loopbacks,
#endif
	},
#ifdef CONFIG_SND_DEBUG
	[ALC880_TEST] = {
		.mixers = { alc880_test_mixer },
		.init_verbs = { alc880_test_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_test_dac_nids),
		.dac_nids = alc880_test_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_test_modes),
		.channel_mode = alc880_test_modes,
		.input_mux = &alc880_test_capture_source,
	},
#endif
};

