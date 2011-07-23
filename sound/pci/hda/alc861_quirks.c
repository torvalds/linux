/*
 * ALC660/ALC861 quirk models
 * included by patch_realtek.c
 */

/* ALC861 models */
enum {
	ALC861_AUTO,
	ALC861_3ST,
	ALC660_3ST,
	ALC861_3ST_DIG,
	ALC861_6ST_DIG,
	ALC861_UNIWILL_M31,
	ALC861_TOSHIBA,
	ALC861_ASUS,
	ALC861_ASUS_LAPTOP,
	ALC861_MODEL_LAST,
};

/*
 *  ALC861 channel source setting (2/6 channel selection for 3-stack)
 */

/*
 * set the path ways for 2 channel output
 * need to set the codec line out and mic 1 pin widgets to inputs
 */
static const struct hda_verb alc861_threestack_ch2_init[] = {
	/* set pin widget 1Ah (line in) for input */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* set pin widget 18h (mic1/2) for input, for mic also enable
	 * the vref
	 */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },

	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c },
#if 0
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8)) }, /*mic*/
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8)) }, /*line-in*/
#endif
	{ } /* end */
};
/*
 * 6ch mode
 * need to set the codec line out and mic 1 pin widgets to outputs
 */
static const struct hda_verb alc861_threestack_ch6_init[] = {
	/* set pin widget 1Ah (line in) for output (Back Surround)*/
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* set pin widget 18h (mic1) for output (CLFE)*/
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },

	{ 0x0c, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x0d, AC_VERB_SET_CONNECT_SEL, 0x00 },

	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080 },
#if 0
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x01 << 8)) }, /*mic*/
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8)) }, /*line in*/
#endif
	{ } /* end */
};

static const struct hda_channel_mode alc861_threestack_modes[2] = {
	{ 2, alc861_threestack_ch2_init },
	{ 6, alc861_threestack_ch6_init },
};
/* Set mic1 as input and unmute the mixer */
static const struct hda_verb alc861_uniwill_m31_ch2_init[] = {
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x01 << 8)) }, /*mic*/
	{ } /* end */
};
/* Set mic1 as output and mute mixer */
static const struct hda_verb alc861_uniwill_m31_ch4_init[] = {
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8)) }, /*mic*/
	{ } /* end */
};

static const struct hda_channel_mode alc861_uniwill_m31_modes[2] = {
	{ 2, alc861_uniwill_m31_ch2_init },
	{ 4, alc861_uniwill_m31_ch4_init },
};

/* Set mic1 and line-in as input and unmute the mixer */
static const struct hda_verb alc861_asus_ch2_init[] = {
	/* set pin widget 1Ah (line in) for input */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* set pin widget 18h (mic1/2) for input, for mic also enable
	 * the vref
	 */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },

	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c },
#if 0
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8)) }, /*mic*/
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8)) }, /*line-in*/
#endif
	{ } /* end */
};
/* Set mic1 nad line-in as output and mute mixer */
static const struct hda_verb alc861_asus_ch6_init[] = {
	/* set pin widget 1Ah (line in) for output (Back Surround)*/
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* { 0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE }, */
	/* set pin widget 18h (mic1) for output (CLFE)*/
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* { 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE }, */
	{ 0x0c, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x0d, AC_VERB_SET_CONNECT_SEL, 0x00 },

	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080 },
#if 0
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x01 << 8)) }, /*mic*/
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8)) }, /*line in*/
#endif
	{ } /* end */
};

static const struct hda_channel_mode alc861_asus_modes[2] = {
	{ 2, alc861_asus_ch2_init },
	{ 6, alc861_asus_ch6_init },
};

/* patch-ALC861 */

static const struct snd_kcontrol_new alc861_base_mixer[] = {
        /* output mixer control */
	HDA_CODEC_MUTE("Front Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x06, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x05, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x05, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Side Playback Switch", 0x04, 0x0, HDA_OUTPUT),

        /*Input mixer control */
	/* HDA_CODEC_VOLUME("Input Playback Volume", 0x15, 0x0, HDA_OUTPUT),
	   HDA_CODEC_MUTE("Input Playback Switch", 0x15, 0x0, HDA_OUTPUT), */
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x10, 0x01, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1a, 0x03, HDA_INPUT),

	{ } /* end */
};

static const struct snd_kcontrol_new alc861_3ST_mixer[] = {
        /* output mixer control */
	HDA_CODEC_MUTE("Front Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x06, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x05, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x05, 2, 0x0, HDA_OUTPUT),
	/*HDA_CODEC_MUTE("Side Playback Switch", 0x04, 0x0, HDA_OUTPUT), */

	/* Input mixer control */
	/* HDA_CODEC_VOLUME("Input Playback Volume", 0x15, 0x0, HDA_OUTPUT),
	   HDA_CODEC_MUTE("Input Playback Switch", 0x15, 0x0, HDA_OUTPUT), */
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x10, 0x01, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1a, 0x03, HDA_INPUT),

	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
                .private_value = ARRAY_SIZE(alc861_threestack_modes),
	},
	{ } /* end */
};

static const struct snd_kcontrol_new alc861_toshiba_mixer[] = {
        /* output mixer control */
	HDA_CODEC_MUTE("Master Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x15, 0x01, HDA_INPUT),

	{ } /* end */
};

static const struct snd_kcontrol_new alc861_uniwill_m31_mixer[] = {
        /* output mixer control */
	HDA_CODEC_MUTE("Front Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x06, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x05, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x05, 2, 0x0, HDA_OUTPUT),
	/*HDA_CODEC_MUTE("Side Playback Switch", 0x04, 0x0, HDA_OUTPUT), */

	/* Input mixer control */
	/* HDA_CODEC_VOLUME("Input Playback Volume", 0x15, 0x0, HDA_OUTPUT),
	   HDA_CODEC_MUTE("Input Playback Switch", 0x15, 0x0, HDA_OUTPUT), */
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x10, 0x01, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1a, 0x03, HDA_INPUT),

	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
                .private_value = ARRAY_SIZE(alc861_uniwill_m31_modes),
	},
	{ } /* end */
};

static const struct snd_kcontrol_new alc861_asus_mixer[] = {
        /* output mixer control */
	HDA_CODEC_MUTE("Front Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x06, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x05, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x05, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Side Playback Switch", 0x04, 0x0, HDA_OUTPUT),

	/* Input mixer control */
	HDA_CODEC_VOLUME("Input Playback Volume", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Input Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x10, 0x01, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1a, 0x03, HDA_OUTPUT),

	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
                .private_value = ARRAY_SIZE(alc861_asus_modes),
	},
	{ }
};

/* additional mixer */
static const struct snd_kcontrol_new alc861_asus_laptop_mixer[] = {
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_INPUT),
	{ }
};

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static const struct hda_verb alc861_base_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	/* port-A for surround (rear panel) */
	{ 0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x0e, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-B for mic-in (rear panel) with vref */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-C for line-in (rear panel) */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* port-D for Front */
	{ 0x0b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x0b, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-E for HP out (front panel) */
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* route front PCM to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-F for mic-in (front panel) with vref */
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-G for CLFE (rear panel) */
	{ 0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x1f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-H for side (rear panel) */
	{ 0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x20, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* CD-in */
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* route front mic to ADC1*/
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Unmute DAC0~3 & spdif out*/
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Unmute Mixer 14 (mic) 1c (Line in)*/
	{0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* Unmute Stereo Mixer 15 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c}, /* Output 0~12 step */

	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* hp used DAC 3 (Front) */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
        {0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},

	{ }
};

static const struct hda_verb alc861_threestack_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	/* port-A for surround (rear panel) */
	{ 0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	/* port-B for mic-in (rear panel) with vref */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-C for line-in (rear panel) */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* port-D for Front */
	{ 0x0b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x0b, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-E for HP out (front panel) */
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* route front PCM to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-F for mic-in (front panel) with vref */
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-G for CLFE (rear panel) */
	{ 0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	/* port-H for side (rear panel) */
	{ 0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	/* CD-in */
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* route front mic to ADC1*/
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* Unmute DAC0~3 & spdif out*/
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Unmute Mixer 14 (mic) 1c (Line in)*/
	{0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* Unmute Stereo Mixer 15 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c}, /* Output 0~12 step */

	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* hp used DAC 3 (Front) */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
        {0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{ }
};

static const struct hda_verb alc861_uniwill_m31_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	/* port-A for surround (rear panel) */
	{ 0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	/* port-B for mic-in (rear panel) with vref */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-C for line-in (rear panel) */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* port-D for Front */
	{ 0x0b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x0b, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-E for HP out (front panel) */
	/* this has to be set to VREF80 */
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* route front PCM to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-F for mic-in (front panel) with vref */
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-G for CLFE (rear panel) */
	{ 0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	/* port-H for side (rear panel) */
	{ 0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	/* CD-in */
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* route front mic to ADC1*/
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* Unmute DAC0~3 & spdif out*/
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Unmute Mixer 14 (mic) 1c (Line in)*/
	{0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* Unmute Stereo Mixer 15 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c}, /* Output 0~12 step */

	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* hp used DAC 3 (Front) */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
        {0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{ }
};

static const struct hda_verb alc861_asus_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	/* port-A for surround (rear panel)
	 * according to codec#0 this is the HP jack
	 */
	{ 0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 }, /* was 0x00 */
	/* route front PCM to HP */
	{ 0x0e, AC_VERB_SET_CONNECT_SEL, 0x01 },
	/* port-B for mic-in (rear panel) with vref */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-C for line-in (rear panel) */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* port-D for Front */
	{ 0x0b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x0b, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-E for HP out (front panel) */
	/* this has to be set to VREF80 */
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* route front PCM to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-F for mic-in (front panel) with vref */
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-G for CLFE (rear panel) */
	{ 0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* port-H for side (rear panel) */
	{ 0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* CD-in */
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* route front mic to ADC1*/
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* Unmute DAC0~3 & spdif out*/
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Unmute Mixer 14 (mic) 1c (Line in)*/
	{0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* Unmute Stereo Mixer 15 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c}, /* Output 0~12 step */

	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* hp used DAC 3 (Front) */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{ }
};

/* additional init verbs for ASUS laptops */
static const struct hda_verb alc861_asus_laptop_init_verbs[] = {
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x45 }, /* HP-out */
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2) }, /* mute line-in */
	{ }
};

static const struct hda_verb alc861_toshiba_init_verbs[] = {
	{0x0f, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_HP_EVENT},

	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc861_toshiba_automute(struct hda_codec *codec)
{
	unsigned int present = snd_hda_jack_detect(codec, 0x0f);

	snd_hda_codec_amp_stereo(codec, 0x16, HDA_INPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x1a, HDA_INPUT, 3,
				 HDA_AMP_MUTE, present ? 0 : HDA_AMP_MUTE);
}

static void alc861_toshiba_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) == ALC_HP_EVENT)
		alc861_toshiba_automute(codec);
}

#define ALC861_DIGOUT_NID	0x07

static const struct hda_channel_mode alc861_8ch_modes[1] = {
	{ 8, NULL }
};

static const hda_nid_t alc861_dac_nids[4] = {
	/* front, surround, clfe, side */
	0x03, 0x06, 0x05, 0x04
};

static const hda_nid_t alc660_dac_nids[3] = {
	/* front, clfe, surround */
	0x03, 0x05, 0x06
};

static const hda_nid_t alc861_adc_nids[1] = {
	/* ADC0-2 */
	0x08,
};

static const struct hda_input_mux alc861_capture_source = {
	.num_items = 5,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x3 },
		{ "Line", 0x1 },
		{ "CD", 0x4 },
		{ "Mixer", 0x5 },
	},
};

/*
 * configuration and preset
 */
static const char * const alc861_models[ALC861_MODEL_LAST] = {
	[ALC861_3ST]		= "3stack",
	[ALC660_3ST]		= "3stack-660",
	[ALC861_3ST_DIG]	= "3stack-dig",
	[ALC861_6ST_DIG]	= "6stack-dig",
	[ALC861_UNIWILL_M31]	= "uniwill-m31",
	[ALC861_TOSHIBA]	= "toshiba",
	[ALC861_ASUS]		= "asus",
	[ALC861_ASUS_LAPTOP]	= "asus-laptop",
	[ALC861_AUTO]		= "auto",
};

static const struct snd_pci_quirk alc861_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1043, 0x1205, "ASUS W7J", ALC861_3ST),
	SND_PCI_QUIRK(0x1043, 0x1335, "ASUS F2/3", ALC861_ASUS_LAPTOP),
	SND_PCI_QUIRK(0x1043, 0x1338, "ASUS F2/3", ALC861_ASUS_LAPTOP),
	SND_PCI_QUIRK(0x1043, 0x1393, "ASUS", ALC861_ASUS),
	SND_PCI_QUIRK(0x1043, 0x13d7, "ASUS A9rp", ALC861_ASUS_LAPTOP),
	SND_PCI_QUIRK(0x1043, 0x81cb, "ASUS P1-AH2", ALC861_3ST_DIG),
	SND_PCI_QUIRK(0x1179, 0xff00, "Toshiba", ALC861_TOSHIBA),
	/* FIXME: the entry below breaks Toshiba A100 (model=auto works!)
	 *        Any other models that need this preset?
	 */
	/* SND_PCI_QUIRK(0x1179, 0xff10, "Toshiba", ALC861_TOSHIBA), */
	SND_PCI_QUIRK(0x1462, 0x7254, "HP dx2200 (MSI MS-7254)", ALC861_3ST),
	SND_PCI_QUIRK(0x1462, 0x7297, "HP dx2250 (MSI MS-7297)", ALC861_3ST),
	SND_PCI_QUIRK(0x1584, 0x2b01, "Uniwill X40AIx", ALC861_UNIWILL_M31),
	SND_PCI_QUIRK(0x1584, 0x9072, "Uniwill m31", ALC861_UNIWILL_M31),
	SND_PCI_QUIRK(0x1584, 0x9075, "Airis Praxis N1212", ALC861_ASUS_LAPTOP),
	/* FIXME: the below seems conflict */
	/* SND_PCI_QUIRK(0x1584, 0x9075, "Uniwill", ALC861_UNIWILL_M31), */
	SND_PCI_QUIRK(0x1849, 0x0660, "Asrock 939SLI32", ALC660_3ST),
	SND_PCI_QUIRK(0x8086, 0xd600, "Intel", ALC861_3ST),
	{}
};

static const struct alc_config_preset alc861_presets[] = {
	[ALC861_3ST] = {
		.mixers = { alc861_3ST_mixer },
		.init_verbs = { alc861_threestack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc861_threestack_modes),
		.channel_mode = alc861_threestack_modes,
		.need_dac_fix = 1,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
	[ALC861_3ST_DIG] = {
		.mixers = { alc861_base_mixer },
		.init_verbs = { alc861_threestack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.dig_out_nid = ALC861_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861_threestack_modes),
		.channel_mode = alc861_threestack_modes,
		.need_dac_fix = 1,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
	[ALC861_6ST_DIG] = {
		.mixers = { alc861_base_mixer },
		.init_verbs = { alc861_base_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.dig_out_nid = ALC861_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861_8ch_modes),
		.channel_mode = alc861_8ch_modes,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
	[ALC660_3ST] = {
		.mixers = { alc861_3ST_mixer },
		.init_verbs = { alc861_threestack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc660_dac_nids),
		.dac_nids = alc660_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc861_threestack_modes),
		.channel_mode = alc861_threestack_modes,
		.need_dac_fix = 1,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
	[ALC861_UNIWILL_M31] = {
		.mixers = { alc861_uniwill_m31_mixer },
		.init_verbs = { alc861_uniwill_m31_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.dig_out_nid = ALC861_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861_uniwill_m31_modes),
		.channel_mode = alc861_uniwill_m31_modes,
		.need_dac_fix = 1,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
	[ALC861_TOSHIBA] = {
		.mixers = { alc861_toshiba_mixer },
		.init_verbs = { alc861_base_init_verbs,
				alc861_toshiba_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
		.unsol_event = alc861_toshiba_unsol_event,
		.init_hook = alc861_toshiba_automute,
	},
	[ALC861_ASUS] = {
		.mixers = { alc861_asus_mixer },
		.init_verbs = { alc861_asus_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.dig_out_nid = ALC861_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861_asus_modes),
		.channel_mode = alc861_asus_modes,
		.need_dac_fix = 1,
		.hp_nid = 0x06,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
	[ALC861_ASUS_LAPTOP] = {
		.mixers = { alc861_toshiba_mixer, alc861_asus_laptop_mixer },
		.init_verbs = { alc861_asus_init_verbs,
				alc861_asus_laptop_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.dig_out_nid = ALC861_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.need_dac_fix = 1,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
};

