/*
 * ALC662/ALC663/ALC665/ALC670 quirk models
 * included by patch_realtek.c
 */

/* ALC662 models */
enum {
	ALC662_AUTO,
	ALC662_3ST_2ch_DIG,
	ALC662_3ST_6ch_DIG,
	ALC662_3ST_6ch,
	ALC662_5ST_DIG,
	ALC662_ASUS_EEEPC_EP20,
	ALC663_ASUS_M51VA,
	ALC663_ASUS_G71V,
	ALC663_ASUS_H13,
	ALC663_ASUS_G50V,
	ALC662_MODEL_LAST,
};

#define ALC662_DIGOUT_NID	0x06
#define ALC662_DIGIN_NID	0x0a

static const hda_nid_t alc662_dac_nids[3] = {
	/* front, rear, clfe */
	0x02, 0x03, 0x04
};

static const hda_nid_t alc272_dac_nids[2] = {
	0x02, 0x03
};

static const hda_nid_t alc662_adc_nids[2] = {
	/* ADC1-2 */
	0x09, 0x08
};

static const hda_nid_t alc272_adc_nids[1] = {
	/* ADC1-2 */
	0x08,
};

static const hda_nid_t alc662_capsrc_nids[2] = { 0x22, 0x23 };
static const hda_nid_t alc272_capsrc_nids[1] = { 0x23 };


/* input MUX */
/* FIXME: should be a matrix-type input source selection */
static const struct hda_input_mux alc662_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static const struct hda_input_mux alc662_lenovo_101e_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x1 },
		{ "Line", 0x2 },
	},
};

static const struct hda_input_mux alc663_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
	},
};

/*
 * 2ch mode
 */
static const struct hda_channel_mode alc662_3ST_2ch_modes[1] = {
	{ 2, NULL }
};

/*
 * 2ch mode
 */
static const struct hda_verb alc662_3ST_ch2_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 6ch mode
 */
static const struct hda_verb alc662_3ST_ch6_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static const struct hda_channel_mode alc662_3ST_6ch_modes[2] = {
	{ 2, alc662_3ST_ch2_init },
	{ 6, alc662_3ST_ch6_init },
};

/*
 * 2ch mode
 */
static const struct hda_verb alc662_sixstack_ch6_init[] = {
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

/*
 * 6ch mode
 */
static const struct hda_verb alc662_sixstack_ch8_init[] = {
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

static const struct hda_channel_mode alc662_5stack_modes[2] = {
	{ 2, alc662_sixstack_ch6_init },
	{ 6, alc662_sixstack_ch8_init },
};

/* Pin assignment: Front=0x14, Rear=0x15, CLFE=0x16, Side=0x17
 *                 Mic=0x18, Front Mic=0x19, Line-In=0x1a, HP=0x1b
 */

static const struct snd_kcontrol_new alc662_base_mixer[] = {
	/* output mixer control */
	HDA_CODEC_VOLUME("Front Playback Volume", 0x2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x0c, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x3, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x0d, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x04, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x04, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x0e, 1, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),

	/*Input mixer control */
	HDA_CODEC_VOLUME("CD Playback Volume", 0xb, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0xb, 0x4, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0xb, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0xb, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0xb, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0xb, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0xb, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0xb, 0x01, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc662_3ST_2ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x0c, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc662_3ST_6ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x0c, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x0d, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x04, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x04, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x0e, 1, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc662_eeepc_ep20_mixer[] = {
	ALC262_HIPPO_MASTER_SWITCH,
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x04, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x04, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("MuteCtrl Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct hda_bind_ctls alc663_asus_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x02, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x03, 3, 0, HDA_OUTPUT),
		0
	},
};

static const struct hda_bind_ctls alc663_asus_one_bind_switch = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x21, 3, 0, HDA_OUTPUT),
		0
	},
};

static const struct snd_kcontrol_new alc663_m51va_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &alc663_asus_bind_master_vol),
	HDA_BIND_SW("Master Playback Switch", &alc663_asus_one_bind_switch),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc663_g71v_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Front Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x21, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc663_g50v_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x21, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc662_chmode_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static const struct hda_verb alc662_init_verbs[] = {
	/* ADC: mute amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},

	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* Front Pin: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Rear Pin: output 1 (0x0d) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* CLFE Pin: output 2 (0x0e) */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

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
	/* Input mixer */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	{ }
};

static const struct hda_verb alc662_eapd_init_verbs[] = {
	/* always trun on EAPD */
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

/* Set Unsolicited Event*/
static const struct hda_verb alc662_eeepc_ep20_sue_init_verbs[] = {
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_HP_EVENT},
	{}
};

static const struct hda_verb alc663_m51va_init_verbs[] = {
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x01},	/* Headphone */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(9)},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_MIC_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_HP_EVENT},
	{}
};

static const struct hda_verb alc663_g71v_init_verbs[] = {
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* {0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, */
	/* {0x15, AC_VERB_SET_CONNECT_SEL, 0x01}, */ /* Headphone */

	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x00},	/* Headphone */

	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC_FRONT_EVENT},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC_MIC_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC_HP_EVENT},
	{}
};

static const struct hda_verb alc663_g50v_init_verbs[] = {
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x00},	/* Headphone */

	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_MIC_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_HP_EVENT},
	{}
};

static void alc662_eeepc_ep20_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x1b;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc663_m51va_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x21;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute_mixer_nid[0] = 0x0c;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_MIXER;
	spec->ext_mic_pin = 0x18;
	spec->int_mic_pin = 0x12;
	spec->auto_mic = 1;
}

static void alc663_g71v_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x21;
	spec->autocfg.line_out_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
	spec->detect_line = 1;
	spec->automute_lines = 1;
	spec->ext_mic_pin = 0x18;
	spec->int_mic_pin = 0x12;
	spec->auto_mic = 1;
}

#define alc663_g50v_setup	alc663_m51va_setup

/*
 * configuration and preset
 */
static const char * const alc662_models[ALC662_MODEL_LAST] = {
	[ALC662_3ST_2ch_DIG]	= "3stack-dig",
	[ALC662_3ST_6ch_DIG]	= "3stack-6ch-dig",
	[ALC662_3ST_6ch]	= "3stack-6ch",
	[ALC662_5ST_DIG]	= "5stack-dig",
	[ALC662_ASUS_EEEPC_EP20] = "eeepc-ep20",
	[ALC663_ASUS_M51VA] = "m51va",
	[ALC663_ASUS_G71V] = "g71v",
	[ALC663_ASUS_H13] = "h13",
	[ALC663_ASUS_G50V] = "g50v",
	[ALC662_AUTO]		= "auto",
};

static const struct snd_pci_quirk alc662_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1043, 0x17c3, "ASUS UX20", ALC663_ASUS_M51VA),
	SND_PCI_QUIRK(0x1043, 0x1878, "ASUS M51VA", ALC663_ASUS_M51VA),
	SND_PCI_QUIRK(0x1043, 0x19a3, "ASUS G50V", ALC663_ASUS_G50V),
	SND_PCI_QUIRK(0x1043, 0x19d3, "ASUS NB", ALC663_ASUS_M51VA),
	SND_PCI_QUIRK(0x1043, 0x8290, "ASUS P5GC-MX", ALC662_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1043, 0x82d1, "ASUS Eeepc EP20", ALC662_ASUS_EEEPC_EP20),
	SND_PCI_QUIRK(0x105b, 0x0d47, "Foxconn 45CMX/45GMX/45CMX-K",
		      ALC662_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1179, 0xff6e, "Toshiba NB20x", ALC662_AUTO),
	SND_PCI_QUIRK(0x1458, 0xa002, "Gigabyte 945GCM-S2L",
		      ALC662_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x152d, 0x2304, "Quanta WH1", ALC663_ASUS_H13),
	SND_PCI_QUIRK(0x1565, 0x820f, "Biostar TA780G M2+", ALC662_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1631, 0xc10c, "PB RS65", ALC663_ASUS_M51VA),
	SND_PCI_QUIRK(0x1849, 0x3662, "ASROCK K10N78FullHD-hSLI R3.0",
					ALC662_3ST_6ch_DIG),
	SND_PCI_QUIRK_MASK(0x1854, 0xf000, 0x2000, "ASUS H13-200x",
			   ALC663_ASUS_H13),
	{}
};

static const struct alc_config_preset alc662_presets[] = {
	[ALC662_3ST_2ch_DIG] = {
		.mixers = { alc662_3ST_2ch_mixer },
		.init_verbs = { alc662_init_verbs, alc662_eapd_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.dig_in_nid = ALC662_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc662_capture_source,
	},
	[ALC662_3ST_6ch_DIG] = {
		.mixers = { alc662_3ST_6ch_mixer, alc662_chmode_mixer },
		.init_verbs = { alc662_init_verbs, alc662_eapd_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.dig_in_nid = ALC662_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_6ch_modes),
		.channel_mode = alc662_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc662_capture_source,
	},
	[ALC662_3ST_6ch] = {
		.mixers = { alc662_3ST_6ch_mixer, alc662_chmode_mixer },
		.init_verbs = { alc662_init_verbs, alc662_eapd_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_6ch_modes),
		.channel_mode = alc662_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc662_capture_source,
	},
	[ALC662_5ST_DIG] = {
		.mixers = { alc662_base_mixer, alc662_chmode_mixer },
		.init_verbs = { alc662_init_verbs, alc662_eapd_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.dig_in_nid = ALC662_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_5stack_modes),
		.channel_mode = alc662_5stack_modes,
		.input_mux = &alc662_capture_source,
	},
	[ALC662_ASUS_EEEPC_EP20] = {
		.mixers = { alc662_eeepc_ep20_mixer,
			    alc662_chmode_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc662_eeepc_ep20_sue_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_6ch_modes),
		.channel_mode = alc662_3ST_6ch_modes,
		.input_mux = &alc662_lenovo_101e_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc662_eeepc_ep20_setup,
		.init_hook = alc_inithook,
	},
	[ALC663_ASUS_M51VA] = {
		.mixers = { alc663_m51va_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc663_m51va_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc663_m51va_setup,
		.init_hook = alc_inithook,
	},
	[ALC663_ASUS_G71V] = {
		.mixers = { alc663_g71v_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc663_g71v_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc663_g71v_setup,
		.init_hook = alc_inithook,
	},
	[ALC663_ASUS_H13] = {
		.mixers = { alc663_m51va_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc663_m51va_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.setup = alc663_m51va_setup,
		.unsol_event = alc_sku_unsol_event,
		.init_hook = alc_inithook,
	},
	[ALC663_ASUS_G50V] = {
		.mixers = { alc663_g50v_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc663_g50v_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_6ch_modes),
		.channel_mode = alc662_3ST_6ch_modes,
		.input_mux = &alc663_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc663_g50v_setup,
		.init_hook = alc_inithook,
	},
};
