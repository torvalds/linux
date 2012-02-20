/*
 * Common codes for Realtek codec quirks
 * included by patch_realtek.c
 */

/*
 * configuration template - to be copied to the spec instance
 */
struct alc_config_preset {
	const struct snd_kcontrol_new *mixers[5]; /* should be identical size
					     * with spec
					     */
	const struct snd_kcontrol_new *cap_mixer; /* capture mixer */
	const struct hda_verb *init_verbs[5];
	unsigned int num_dacs;
	const hda_nid_t *dac_nids;
	hda_nid_t dig_out_nid;		/* optional */
	hda_nid_t hp_nid;		/* optional */
	const hda_nid_t *slave_dig_outs;
	unsigned int num_adc_nids;
	const hda_nid_t *adc_nids;
	const hda_nid_t *capsrc_nids;
	hda_nid_t dig_in_nid;
	unsigned int num_channel_mode;
	const struct hda_channel_mode *channel_mode;
	int need_dac_fix;
	int const_channel_count;
	unsigned int num_mux_defs;
	const struct hda_input_mux *input_mux;
	void (*unsol_event)(struct hda_codec *, unsigned int);
	void (*setup)(struct hda_codec *);
	void (*init_hook)(struct hda_codec *);
#ifdef CONFIG_SND_HDA_POWER_SAVE
	const struct hda_amp_list *loopbacks;
	void (*power_hook)(struct hda_codec *codec);
#endif
};

/*
 * channel mode setting
 */
static int alc_ch_mode_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	return snd_hda_ch_mode_info(codec, uinfo, spec->channel_mode,
				    spec->num_channel_mode);
}

static int alc_ch_mode_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	return snd_hda_ch_mode_get(codec, ucontrol, spec->channel_mode,
				   spec->num_channel_mode,
				   spec->ext_channel_count);
}

static int alc_ch_mode_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	int err = snd_hda_ch_mode_put(codec, ucontrol, spec->channel_mode,
				      spec->num_channel_mode,
				      &spec->ext_channel_count);
	if (err >= 0 && !spec->const_channel_count) {
		spec->multiout.max_channels = spec->ext_channel_count;
		if (spec->need_dac_fix)
			spec->multiout.num_dacs = spec->multiout.max_channels / 2;
	}
	return err;
}

static void alc_fixup_autocfg_pin_nums(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;

	if (!cfg->line_outs) {
		while (cfg->line_outs < AUTO_CFG_MAX_OUTS &&
		       cfg->line_out_pins[cfg->line_outs])
			cfg->line_outs++;
	}
	if (!cfg->speaker_outs) {
		while (cfg->speaker_outs < AUTO_CFG_MAX_OUTS &&
		       cfg->speaker_pins[cfg->speaker_outs])
			cfg->speaker_outs++;
	}
	if (!cfg->hp_outs) {
		while (cfg->hp_outs < AUTO_CFG_MAX_OUTS &&
		       cfg->hp_pins[cfg->hp_outs])
			cfg->hp_outs++;
	}
}

/*
 * set up from the preset table
 */
static void setup_preset(struct hda_codec *codec,
			 const struct alc_config_preset *preset)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < ARRAY_SIZE(preset->mixers) && preset->mixers[i]; i++)
		add_mixer(spec, preset->mixers[i]);
	spec->cap_mixer = preset->cap_mixer;
	for (i = 0; i < ARRAY_SIZE(preset->init_verbs) && preset->init_verbs[i];
	     i++)
		add_verb(spec, preset->init_verbs[i]);

	spec->channel_mode = preset->channel_mode;
	spec->num_channel_mode = preset->num_channel_mode;
	spec->need_dac_fix = preset->need_dac_fix;
	spec->const_channel_count = preset->const_channel_count;

	if (preset->const_channel_count)
		spec->multiout.max_channels = preset->const_channel_count;
	else
		spec->multiout.max_channels = spec->channel_mode[0].channels;
	spec->ext_channel_count = spec->channel_mode[0].channels;

	spec->multiout.num_dacs = preset->num_dacs;
	spec->multiout.dac_nids = preset->dac_nids;
	spec->multiout.dig_out_nid = preset->dig_out_nid;
	spec->multiout.slave_dig_outs = preset->slave_dig_outs;
	spec->multiout.hp_nid = preset->hp_nid;

	spec->num_mux_defs = preset->num_mux_defs;
	if (!spec->num_mux_defs)
		spec->num_mux_defs = 1;
	spec->input_mux = preset->input_mux;

	spec->num_adc_nids = preset->num_adc_nids;
	spec->adc_nids = preset->adc_nids;
	spec->capsrc_nids = preset->capsrc_nids;
	spec->dig_in_nid = preset->dig_in_nid;

	spec->unsol_event = preset->unsol_event;
	spec->init_hook = preset->init_hook;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->power_hook = preset->power_hook;
	spec->loopback.amplist = preset->loopbacks;
#endif

	if (preset->setup)
		preset->setup(codec);

	alc_fixup_autocfg_pin_nums(codec);
}

static void alc_simple_setup_automute(struct alc_spec *spec, int mode)
{
	int lo_pin = spec->autocfg.line_out_pins[0];

	if (lo_pin == spec->autocfg.speaker_pins[0] ||
		lo_pin == spec->autocfg.hp_pins[0])
		lo_pin = 0;
	spec->automute_mode = mode;
	spec->detect_hp = !!spec->autocfg.hp_pins[0];
	spec->detect_lo = !!lo_pin;
	spec->automute_lo = spec->automute_lo_possible = !!lo_pin;
	spec->automute_speaker = spec->automute_speaker_possible = !!spec->autocfg.speaker_pins[0];
}
