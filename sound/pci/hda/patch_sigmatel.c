/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for SigmaTel STAC92xx
 *
 * Copyright (c) 2005 Embedded Alley Solutions, Inc.
 * Matt Porter <mporter@embeddedalley.com>
 *
 * Based on patch_cmedia.c and patch_realtek.c
 * Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <sound/core.h>
#include <sound/asoundef.h>
#include "hda_codec.h"
#include "hda_local.h"

#define NUM_CONTROL_ALLOC	32
#define STAC_HP_EVENT		0x37
#define STAC_UNSOL_ENABLE 	(AC_USRSP_EN | STAC_HP_EVENT)

#define STAC_REF		0
#define STAC_D945GTP3		1
#define STAC_D945GTP5		2

struct sigmatel_spec {
	struct snd_kcontrol_new *mixers[4];
	unsigned int num_mixers;

	int board_config;
	unsigned int surr_switch: 1;
	unsigned int line_switch: 1;
	unsigned int mic_switch: 1;
	unsigned int alt_switch: 1;

	/* playback */
	struct hda_multi_out multiout;
	hda_nid_t dac_nids[5];

	/* capture */
	hda_nid_t *adc_nids;
	unsigned int num_adcs;
	hda_nid_t *mux_nids;
	unsigned int num_muxes;
	hda_nid_t dig_in_nid;

	/* pin widgets */
	hda_nid_t *pin_nids;
	unsigned int num_pins;
	unsigned int *pin_configs;

	/* codec specific stuff */
	struct hda_verb *init;
	struct snd_kcontrol_new *mixer;

	/* capture source */
	struct hda_input_mux *input_mux;
	unsigned int cur_mux[3];

	/* i/o switches */
	unsigned int io_switch[2];

	struct hda_pcm pcm_rec[2];	/* PCM information */

	/* dynamic controls and input_mux */
	struct auto_pin_cfg autocfg;
	unsigned int num_kctl_alloc, num_kctl_used;
	struct snd_kcontrol_new *kctl_alloc;
	struct hda_input_mux private_imux;
};

static hda_nid_t stac9200_adc_nids[1] = {
        0x03,
};

static hda_nid_t stac9200_mux_nids[1] = {
        0x0c,
};

static hda_nid_t stac9200_dac_nids[1] = {
        0x02,
};

static hda_nid_t stac922x_adc_nids[2] = {
        0x06, 0x07,
};

static hda_nid_t stac922x_mux_nids[2] = {
        0x12, 0x13,
};

static hda_nid_t stac927x_adc_nids[3] = {
        0x07, 0x08, 0x09
};

static hda_nid_t stac927x_mux_nids[3] = {
        0x15, 0x16, 0x17
};

static hda_nid_t stac9200_pin_nids[8] = {
	0x08, 0x09, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,
};

static hda_nid_t stac922x_pin_nids[10] = {
	0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
	0x0f, 0x10, 0x11, 0x15, 0x1b,
};

static hda_nid_t stac927x_pin_nids[14] = {
	0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
	0x0f, 0x10, 0x11, 0x12, 0x13,
	0x14, 0x21, 0x22, 0x23,
};

static int stac92xx_mux_enum_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_input_mux_info(spec->input_mux, uinfo);
}

static int stac92xx_mux_enum_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_mux[adc_idx];
	return 0;
}

static int stac92xx_mux_enum_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
				     spec->mux_nids[adc_idx], &spec->cur_mux[adc_idx]);
}

static struct hda_verb stac9200_core_init[] = {
	/* set dac0mux for dac converter */
	{ 0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{}
};

static struct hda_verb stac922x_core_init[] = {
	/* set master volume and direct control */	
	{ 0x16, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xff},
	{}
};

static struct hda_verb stac927x_core_init[] = {
	/* set master volume and direct control */	
	{ 0x24, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xff},
	{}
};

static struct snd_kcontrol_new stac9200_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0xb, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Master Playback Switch", 0xb, 0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Input Source",
		.count = 1,
		.info = stac92xx_mux_enum_info,
		.get = stac92xx_mux_enum_get,
		.put = stac92xx_mux_enum_put,
	},
	HDA_CODEC_VOLUME("Capture Volume", 0x0a, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x0a, 0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Mux Volume", 0x0c, 0, HDA_OUTPUT),
	{ } /* end */
};

/* This needs to be generated dynamically based on sequence */
static struct snd_kcontrol_new stac922x_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Input Source",
		.count = 1,
		.info = stac92xx_mux_enum_info,
		.get = stac92xx_mux_enum_get,
		.put = stac92xx_mux_enum_put,
	},
	HDA_CODEC_VOLUME("Capture Volume", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mux Capture Volume", 0x12, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static snd_kcontrol_new_t stac927x_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Input Source",
		.count = 1,
		.info = stac92xx_mux_enum_info,
		.get = stac92xx_mux_enum_get,
		.put = stac92xx_mux_enum_put,
	},
	HDA_CODEC_VOLUME("InMux Capture Volume", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("InVol Capture Volume", 0x18, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("ADCMux Capture Switch", 0x1b, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static int stac92xx_build_controls(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int err;
	int i;

	err = snd_hda_add_new_ctls(codec, spec->mixer);
	if (err < 0)
		return err;

	for (i = 0; i < spec->num_mixers; i++) {
		err = snd_hda_add_new_ctls(codec, spec->mixers[i]);
		if (err < 0)
			return err;
	}

	if (spec->multiout.dig_out_nid) {
		err = snd_hda_create_spdif_out_ctls(codec, spec->multiout.dig_out_nid);
		if (err < 0)
			return err;
	}
	if (spec->dig_in_nid) {
		err = snd_hda_create_spdif_in_ctls(codec, spec->dig_in_nid);
		if (err < 0)
			return err;
	}
	return 0;	
}

static unsigned int ref9200_pin_configs[8] = {
	0x01c47010, 0x01447010, 0x0221401f, 0x01114010,
	0x02a19020, 0x01a19021, 0x90100140, 0x01813122,
};

static unsigned int *stac9200_brd_tbl[] = {
	ref9200_pin_configs,
};

static struct hda_board_config stac9200_cfg_tbl[] = {
	{ .modelname = "ref",
	  .pci_subvendor = PCI_VENDOR_ID_INTEL,
	  .pci_subdevice = 0x2668,	/* DFI LanParty */
	  .config = STAC_REF },
	{} /* terminator */
};

static unsigned int ref922x_pin_configs[10] = {
	0x01014010, 0x01016011, 0x01012012, 0x0221401f,
	0x01813122, 0x01011014, 0x01441030, 0x01c41030,
	0x40000100, 0x40000100,
};

static unsigned int d945gtp3_pin_configs[10] = {
	0x0221401f, 0x01a19022, 0x01813021, 0x01014010,
	0x40000100, 0x40000100, 0x40000100, 0x40000100,
	0x02a19120, 0x40000100,
};

static unsigned int d945gtp5_pin_configs[10] = {
	0x0221401f, 0x01011012, 0x01813024, 0x01014010,
	0x01a19021, 0x01016011, 0x01452130, 0x40000100,
	0x02a19320, 0x40000100,
};

static unsigned int *stac922x_brd_tbl[] = {
	ref922x_pin_configs,
	d945gtp3_pin_configs,
	d945gtp5_pin_configs,
};

static struct hda_board_config stac922x_cfg_tbl[] = {
	{ .modelname = "ref",
	  .pci_subvendor = PCI_VENDOR_ID_INTEL,
	  .pci_subdevice = 0x2668,	/* DFI LanParty */
	  .config = STAC_REF },		/* SigmaTel reference board */
	{ .pci_subvendor = PCI_VENDOR_ID_INTEL,
	  .pci_subdevice = 0x0101,
	  .config = STAC_D945GTP3 },	/* Intel D945GTP - 3 Stack */
	{ .pci_subvendor = PCI_VENDOR_ID_INTEL,
	  .pci_subdevice = 0x0404,
	  .config = STAC_D945GTP5 },	/* Intel D945GTP - 5 Stack */
	{ .pci_subvendor = PCI_VENDOR_ID_INTEL,
	  .pci_subdevice = 0x0303,
	  .config = STAC_D945GTP5 },	/* Intel D945GNT - 5 Stack */
	{ .pci_subvendor = PCI_VENDOR_ID_INTEL,
	  .pci_subdevice = 0x0013,
	  .config = STAC_D945GTP5 },	/* Intel D955XBK - 5 Stack */
	{ .pci_subvendor = PCI_VENDOR_ID_INTEL,
	  .pci_subdevice = 0x0417,
	  .config = STAC_D945GTP5 },	/* Intel D975XBK - 5 Stack */
	{} /* terminator */
};

static unsigned int ref927x_pin_configs[14] = {
	0x01813122, 0x01a19021, 0x01014010, 0x01016011,
	0x01012012, 0x01011014, 0x40000100, 0x40000100, 
	0x40000100, 0x40000100, 0x40000100, 0x01441030,
	0x01c41030, 0x40000100,
};

static unsigned int *stac927x_brd_tbl[] = {
	ref927x_pin_configs,
};

static struct hda_board_config stac927x_cfg_tbl[] = {
	{ .modelname = "ref",
	  .pci_subvendor = PCI_VENDOR_ID_INTEL,
	  .pci_subdevice = 0x2668,	/* DFI LanParty */
	  .config = STAC_REF },		/* SigmaTel reference board */
	{} /* terminator */
};

static void stac92xx_set_config_regs(struct hda_codec *codec)
{
	int i;
	struct sigmatel_spec *spec = codec->spec;
	unsigned int pin_cfg;

	for (i=0; i < spec->num_pins; i++) {
		snd_hda_codec_write(codec, spec->pin_nids[i], 0,
				    AC_VERB_SET_CONFIG_DEFAULT_BYTES_0,
				    spec->pin_configs[i] & 0x000000ff);
		snd_hda_codec_write(codec, spec->pin_nids[i], 0,
				    AC_VERB_SET_CONFIG_DEFAULT_BYTES_1,
				    (spec->pin_configs[i] & 0x0000ff00) >> 8);
		snd_hda_codec_write(codec, spec->pin_nids[i], 0,
				    AC_VERB_SET_CONFIG_DEFAULT_BYTES_2,
				    (spec->pin_configs[i] & 0x00ff0000) >> 16);
		snd_hda_codec_write(codec, spec->pin_nids[i], 0,
				    AC_VERB_SET_CONFIG_DEFAULT_BYTES_3,
				    spec->pin_configs[i] >> 24);
		pin_cfg = snd_hda_codec_read(codec, spec->pin_nids[i], 0,
					     AC_VERB_GET_CONFIG_DEFAULT,
					     0x00);	
		snd_printdd(KERN_INFO "hda_codec: pin nid %2.2x pin config %8.8x\n", spec->pin_nids[i], pin_cfg);
	}
}

/*
 * Analog playback callbacks
 */
static int stac92xx_playback_pcm_open(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream);
}

static int stac92xx_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 unsigned int stream_tag,
					 unsigned int format,
					 struct snd_pcm_substream *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_prepare(codec, &spec->multiout, stream_tag, format, substream);
}

static int stac92xx_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

/*
 * Digital playback callbacks
 */
static int stac92xx_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
					  struct hda_codec *codec,
					  struct snd_pcm_substream *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int stac92xx_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
					   struct hda_codec *codec,
					   struct snd_pcm_substream *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}


/*
 * Analog capture callbacks
 */
static int stac92xx_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					unsigned int stream_tag,
					unsigned int format,
					struct snd_pcm_substream *substream)
{
	struct sigmatel_spec *spec = codec->spec;

	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number],
                                   stream_tag, 0, format);
	return 0;
}

static int stac92xx_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct sigmatel_spec *spec = codec->spec;

	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number], 0, 0, 0);
	return 0;
}

static struct hda_pcm_stream stac92xx_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in stac92xx_build_pcms */
	.ops = {
		.open = stac92xx_dig_playback_pcm_open,
		.close = stac92xx_dig_playback_pcm_close
	},
};

static struct hda_pcm_stream stac92xx_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in stac92xx_build_pcms */
};

static struct hda_pcm_stream stac92xx_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	.nid = 0x02, /* NID to query formats and rates */
	.ops = {
		.open = stac92xx_playback_pcm_open,
		.prepare = stac92xx_playback_pcm_prepare,
		.cleanup = stac92xx_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream stac92xx_pcm_analog_alt_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x06, /* NID to query formats and rates */
	.ops = {
		.open = stac92xx_playback_pcm_open,
		.prepare = stac92xx_playback_pcm_prepare,
		.cleanup = stac92xx_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream stac92xx_pcm_analog_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in stac92xx_build_pcms */
	.ops = {
		.prepare = stac92xx_capture_pcm_prepare,
		.cleanup = stac92xx_capture_pcm_cleanup
	},
};

static int stac92xx_build_pcms(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = "STAC92xx Analog";
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = stac92xx_pcm_analog_playback;
	info->stream[SNDRV_PCM_STREAM_CAPTURE] = stac92xx_pcm_analog_capture;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adc_nids[0];

	if (spec->alt_switch) {
		codec->num_pcms++;
		info++;
		info->name = "STAC92xx Analog Alt";
		info->stream[SNDRV_PCM_STREAM_PLAYBACK] = stac92xx_pcm_analog_alt_playback;
	}

	if (spec->multiout.dig_out_nid || spec->dig_in_nid) {
		codec->num_pcms++;
		info++;
		info->name = "STAC92xx Digital";
		if (spec->multiout.dig_out_nid) {
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] = stac92xx_pcm_digital_playback;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->multiout.dig_out_nid;
		}
		if (spec->dig_in_nid) {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] = stac92xx_pcm_digital_capture;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->dig_in_nid;
		}
	}

	return 0;
}

static void stac92xx_auto_set_pinctl(struct hda_codec *codec, hda_nid_t nid, int pin_type)

{
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, pin_type);
}

static int stac92xx_io_switch_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int stac92xx_io_switch_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	int io_idx = kcontrol-> private_value & 0xff;

	ucontrol->value.integer.value[0] = spec->io_switch[io_idx];
	return 0;
}

static int stac92xx_io_switch_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
        struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
        hda_nid_t nid = kcontrol->private_value >> 8;
	int io_idx = kcontrol-> private_value & 0xff;
        unsigned short val = ucontrol->value.integer.value[0];

	spec->io_switch[io_idx] = val;

	if (val)
		stac92xx_auto_set_pinctl(codec, nid, AC_PINCTL_OUT_EN);
	else
		stac92xx_auto_set_pinctl(codec, nid, AC_PINCTL_IN_EN);

        return 1;
}

#define STAC_CODEC_IO_SWITCH(xname, xpval) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	  .name = xname, \
	  .index = 0, \
          .info = stac92xx_io_switch_info, \
          .get = stac92xx_io_switch_get, \
          .put = stac92xx_io_switch_put, \
          .private_value = xpval, \
	}


enum {
	STAC_CTL_WIDGET_VOL,
	STAC_CTL_WIDGET_MUTE,
	STAC_CTL_WIDGET_IO_SWITCH,
};

static struct snd_kcontrol_new stac92xx_control_templates[] = {
	HDA_CODEC_VOLUME(NULL, 0, 0, 0),
	HDA_CODEC_MUTE(NULL, 0, 0, 0),
	STAC_CODEC_IO_SWITCH(NULL, 0),
};

/* add dynamic controls */
static int stac92xx_add_control(struct sigmatel_spec *spec, int type, const char *name, unsigned long val)
{
	struct snd_kcontrol_new *knew;

	if (spec->num_kctl_used >= spec->num_kctl_alloc) {
		int num = spec->num_kctl_alloc + NUM_CONTROL_ALLOC;

		knew = kcalloc(num + 1, sizeof(*knew), GFP_KERNEL); /* array + terminator */
		if (! knew)
			return -ENOMEM;
		if (spec->kctl_alloc) {
			memcpy(knew, spec->kctl_alloc, sizeof(*knew) * spec->num_kctl_alloc);
			kfree(spec->kctl_alloc);
		}
		spec->kctl_alloc = knew;
		spec->num_kctl_alloc = num;
	}

	knew = &spec->kctl_alloc[spec->num_kctl_used];
	*knew = stac92xx_control_templates[type];
	knew->name = kstrdup(name, GFP_KERNEL);
	if (! knew->name)
		return -ENOMEM;
	knew->private_value = val;
	spec->num_kctl_used++;
	return 0;
}

/* flag inputs as additional dynamic lineouts */
static int stac92xx_add_dyn_out_pins(struct hda_codec *codec, struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;

	switch (cfg->line_outs) {
	case 3:
		/* add line-in as side */
		if (cfg->input_pins[AUTO_PIN_LINE]) {
			cfg->line_out_pins[3] = cfg->input_pins[AUTO_PIN_LINE];
			spec->line_switch = 1;
			cfg->line_outs++;
		}
		break;
	case 2:
		/* add line-in as clfe and mic as side */
		if (cfg->input_pins[AUTO_PIN_LINE]) {
			cfg->line_out_pins[2] = cfg->input_pins[AUTO_PIN_LINE];
			spec->line_switch = 1;
			cfg->line_outs++;
		}
		if (cfg->input_pins[AUTO_PIN_MIC]) {
			cfg->line_out_pins[3] = cfg->input_pins[AUTO_PIN_MIC];
			spec->mic_switch = 1;
			cfg->line_outs++;
		}
		break;
	case 1:
		/* add line-in as surr and mic as clfe */
		if (cfg->input_pins[AUTO_PIN_LINE]) {
			cfg->line_out_pins[1] = cfg->input_pins[AUTO_PIN_LINE];
			spec->line_switch = 1;
			cfg->line_outs++;
		}
		if (cfg->input_pins[AUTO_PIN_MIC]) {
			cfg->line_out_pins[2] = cfg->input_pins[AUTO_PIN_MIC];
			spec->mic_switch = 1;
			cfg->line_outs++;
		}
		break;
	}

	return 0;
}

/*
 * XXX The line_out pin widget connection list may not be set to the
 * desired DAC nid. This is the case on 927x where ports A and B can
 * be routed to several DACs.
 *
 * This requires an analysis of the line-out/hp pin configuration
 * to provide a best fit for pin/DAC configurations that are routable.
 * For now, 927x DAC4 is not supported and 927x DAC1 output to ports
 * A and B is not supported.
 */
/* fill in the dac_nids table from the parsed pin configuration */
static int stac92xx_auto_fill_dac_nids(struct hda_codec *codec, const struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;
	hda_nid_t nid;
	int i;

	/* check the pins hardwired to audio widget */
	for (i = 0; i < cfg->line_outs; i++) {
		nid = cfg->line_out_pins[i];
		spec->multiout.dac_nids[i] = snd_hda_codec_read(codec, nid, 0,
					AC_VERB_GET_CONNECT_LIST, 0) & 0xff;
	}

	if (cfg->line_outs)
		spec->multiout.num_dacs = cfg->line_outs;
	else if (cfg->hp_pin) {
		spec->multiout.dac_nids[0] = snd_hda_codec_read(codec, cfg->hp_pin, 0,
					AC_VERB_GET_CONNECT_LIST, 0) & 0xff;
		spec->multiout.num_dacs = 1;
	}

	return 0;
}

/* add playback controls from the parsed DAC table */
static int stac92xx_auto_create_multi_out_ctls(struct sigmatel_spec *spec, const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = { "Front", "Surround", NULL /*CLFE*/, "Side" };
	hda_nid_t nid;
	int i, err;

	for (i = 0; i < cfg->line_outs; i++) {
		if (!spec->multiout.dac_nids[i])
			continue;

		nid = spec->multiout.dac_nids[i];

		if (i == 2) {
			/* Center/LFE */
			if ((err = stac92xx_add_control(spec, STAC_CTL_WIDGET_VOL, "Center Playback Volume",
					       HDA_COMPOSE_AMP_VAL(nid, 1, 0, HDA_OUTPUT))) < 0)
				return err;
			if ((err = stac92xx_add_control(spec, STAC_CTL_WIDGET_VOL, "LFE Playback Volume",
					       HDA_COMPOSE_AMP_VAL(nid, 2, 0, HDA_OUTPUT))) < 0)
				return err;
			if ((err = stac92xx_add_control(spec, STAC_CTL_WIDGET_MUTE, "Center Playback Switch",
					       HDA_COMPOSE_AMP_VAL(nid, 1, 0, HDA_OUTPUT))) < 0)
				return err;
			if ((err = stac92xx_add_control(spec, STAC_CTL_WIDGET_MUTE, "LFE Playback Switch",
					       HDA_COMPOSE_AMP_VAL(nid, 2, 0, HDA_OUTPUT))) < 0)
				return err;
		} else {
			sprintf(name, "%s Playback Volume", chname[i]);
			if ((err = stac92xx_add_control(spec, STAC_CTL_WIDGET_VOL, name,
					       HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT))) < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			if ((err = stac92xx_add_control(spec, STAC_CTL_WIDGET_MUTE, name,
					       HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT))) < 0)
				return err;
		}
	}

	if (spec->line_switch)
		if ((err = stac92xx_add_control(spec, STAC_CTL_WIDGET_IO_SWITCH, "Line In as Output Switch", cfg->input_pins[AUTO_PIN_LINE] << 8)) < 0)
			return err;

	if (spec->mic_switch)
		if ((err = stac92xx_add_control(spec, STAC_CTL_WIDGET_IO_SWITCH, "Mic as Output Switch", (cfg->input_pins[AUTO_PIN_MIC] << 8) | 1)) < 0)
			return err;

	return 0;
}

/* add playback controls for HP output */
static int stac92xx_auto_create_hp_ctls(struct hda_codec *codec, struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;
	hda_nid_t pin = cfg->hp_pin;
	hda_nid_t nid;
	int i, err;
	unsigned int wid_caps;

	if (! pin)
		return 0;

	wid_caps = get_wcaps(codec, pin);
	if (wid_caps & AC_WCAP_UNSOL_CAP)
		/* Enable unsolicited responses on the HP widget */
		snd_hda_codec_write(codec, pin, 0,
				AC_VERB_SET_UNSOLICITED_ENABLE,
				STAC_UNSOL_ENABLE);

	nid = snd_hda_codec_read(codec, pin, 0, AC_VERB_GET_CONNECT_LIST, 0) & 0xff;
	for (i = 0; i < cfg->line_outs; i++) {
		if (! spec->multiout.dac_nids[i])
			continue;
		if (spec->multiout.dac_nids[i] == nid)
			return 0;
	}

	spec->multiout.hp_nid = nid;

	/* control HP volume/switch on the output mixer amp */
	if ((err = stac92xx_add_control(spec, STAC_CTL_WIDGET_VOL, "Headphone Playback Volume",
					HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT))) < 0)
		return err;
	if ((err = stac92xx_add_control(spec, STAC_CTL_WIDGET_MUTE, "Headphone Playback Switch",
					HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT))) < 0)
		return err;

	return 0;
}

/* create playback/capture controls for input pins */
static int stac92xx_auto_create_analog_input_ctls(struct hda_codec *codec, const struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->private_imux;
	hda_nid_t con_lst[HDA_MAX_NUM_INPUTS];
	int i, j, k;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		int index = -1;
		if (cfg->input_pins[i]) {
			/* Enable active pin widget as an input */
			stac92xx_auto_set_pinctl(codec, cfg->input_pins[i], AC_PINCTL_IN_EN);

			imux->items[imux->num_items].label = auto_pin_cfg_labels[i];

			for (j=0; j<spec->num_muxes; j++) {
				int num_cons = snd_hda_get_connections(codec, spec->mux_nids[j], con_lst, HDA_MAX_NUM_INPUTS);
				for (k=0; k<num_cons; k++)
					if (con_lst[k] == cfg->input_pins[i]) {
						index = k;
					 	break;
					}
				if (index >= 0)
					break;
			}
			imux->items[imux->num_items].index = index;
			imux->num_items++;
		}
	}

	return 0;
}

static void stac92xx_auto_init_multi_out(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->autocfg.line_outs; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		stac92xx_auto_set_pinctl(codec, nid, AC_PINCTL_OUT_EN);
	}
}

static void stac92xx_auto_init_hp_out(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.hp_pin;
	if (pin) /* connect to front */
		stac92xx_auto_set_pinctl(codec, pin, AC_PINCTL_OUT_EN | AC_PINCTL_HP_EN);
}

static int stac92xx_parse_auto_config(struct hda_codec *codec, hda_nid_t dig_out, hda_nid_t dig_in)
{
	struct sigmatel_spec *spec = codec->spec;
	int err;

	if ((err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL)) < 0)
		return err;
	if (! spec->autocfg.line_outs && ! spec->autocfg.hp_pin)
		return 0; /* can't find valid pin config */
	stac92xx_auto_init_multi_out(codec);
	stac92xx_auto_init_hp_out(codec);
	if ((err = stac92xx_add_dyn_out_pins(codec, &spec->autocfg)) < 0)
		return err;
	if ((err = stac92xx_auto_fill_dac_nids(codec, &spec->autocfg)) < 0)
		return err;

	if ((err = stac92xx_auto_create_multi_out_ctls(spec, &spec->autocfg)) < 0 ||
	    (err = stac92xx_auto_create_hp_ctls(codec, &spec->autocfg)) < 0 ||
	    (err = stac92xx_auto_create_analog_input_ctls(codec, &spec->autocfg)) < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;
	if (spec->multiout.max_channels > 2)
		spec->surr_switch = 1;

	if (spec->autocfg.dig_out_pin) {
		spec->multiout.dig_out_nid = dig_out;
		stac92xx_auto_set_pinctl(codec, spec->autocfg.dig_out_pin, AC_PINCTL_OUT_EN);
	}
	if (spec->autocfg.dig_in_pin) {
		spec->dig_in_nid = dig_in;
		stac92xx_auto_set_pinctl(codec, spec->autocfg.dig_in_pin, AC_PINCTL_IN_EN);
	}

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->input_mux = &spec->private_imux;

	return 1;
}

static int stac9200_parse_auto_config(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int err;

	if ((err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL)) < 0)
		return err;

	if ((err = stac92xx_auto_create_analog_input_ctls(codec, &spec->autocfg)) < 0)
		return err;

	if (spec->autocfg.dig_out_pin) {
		spec->multiout.dig_out_nid = 0x05;
		stac92xx_auto_set_pinctl(codec, spec->autocfg.dig_out_pin, AC_PINCTL_OUT_EN);
	}
	if (spec->autocfg.dig_in_pin) {
		spec->dig_in_nid = 0x04;
		stac92xx_auto_set_pinctl(codec, spec->autocfg.dig_in_pin, AC_PINCTL_IN_EN);
	}

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->input_mux = &spec->private_imux;

	return 1;
}

static int stac92xx_init(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;

	snd_hda_sequence_write(codec, spec->init);

	return 0;
}

static void stac92xx_free(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int i;

	if (! spec)
		return;

	if (spec->kctl_alloc) {
		for (i = 0; i < spec->num_kctl_used; i++)
			kfree(spec->kctl_alloc[i].name);
		kfree(spec->kctl_alloc);
	}

	kfree(spec);
}

static void stac92xx_set_pinctl(struct hda_codec *codec, hda_nid_t nid,
				unsigned int flag)
{
	unsigned int pin_ctl = snd_hda_codec_read(codec, nid,
			0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00);
	snd_hda_codec_write(codec, nid, 0,
			AC_VERB_SET_PIN_WIDGET_CONTROL,
			pin_ctl | flag);
}

static void stac92xx_reset_pinctl(struct hda_codec *codec, hda_nid_t nid,
				  unsigned int flag)
{
	unsigned int pin_ctl = snd_hda_codec_read(codec, nid,
			0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00);
	snd_hda_codec_write(codec, nid, 0,
			AC_VERB_SET_PIN_WIDGET_CONTROL,
			pin_ctl & ~flag);
}

static void stac92xx_unsol_event(struct hda_codec *codec, unsigned int res)
{
	struct sigmatel_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i, presence;

	if ((res >> 26) != STAC_HP_EVENT)
		return;

	presence = snd_hda_codec_read(codec, cfg->hp_pin, 0,
			AC_VERB_GET_PIN_SENSE, 0x00) >> 31;

	if (presence) {
		/* disable lineouts, enable hp */
		for (i = 0; i < cfg->line_outs; i++)
			stac92xx_reset_pinctl(codec, cfg->line_out_pins[i],
						AC_PINCTL_OUT_EN);
		stac92xx_set_pinctl(codec, cfg->hp_pin, AC_PINCTL_OUT_EN);
	} else {
		/* enable lineouts, disable hp */
		for (i = 0; i < cfg->line_outs; i++)
			stac92xx_set_pinctl(codec, cfg->line_out_pins[i],
						AC_PINCTL_OUT_EN);
		stac92xx_reset_pinctl(codec, cfg->hp_pin, AC_PINCTL_OUT_EN);
	}
} 

#ifdef CONFIG_PM
static int stac92xx_resume(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int i;

	stac92xx_init(codec);
	for (i = 0; i < spec->num_mixers; i++)
		snd_hda_resume_ctls(codec, spec->mixers[i]);
	if (spec->multiout.dig_out_nid)
		snd_hda_resume_spdif_out(codec);
	if (spec->dig_in_nid)
		snd_hda_resume_spdif_in(codec);

	return 0;
}
#endif

static struct hda_codec_ops stac92xx_patch_ops = {
	.build_controls = stac92xx_build_controls,
	.build_pcms = stac92xx_build_pcms,
	.init = stac92xx_init,
	.free = stac92xx_free,
	.unsol_event = stac92xx_unsol_event,
#ifdef CONFIG_PM
	.resume = stac92xx_resume,
#endif
};

static int patch_stac9200(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	spec->board_config = snd_hda_check_board_config(codec, stac9200_cfg_tbl);
	if (spec->board_config < 0)
                snd_printdd(KERN_INFO "hda_codec: Unknown model for STAC9200, using BIOS defaults\n");
	else {
		spec->num_pins = 8;
		spec->pin_nids = stac9200_pin_nids;
		spec->pin_configs = stac9200_brd_tbl[spec->board_config];
		stac92xx_set_config_regs(codec);
	}

	spec->multiout.max_channels = 2;
	spec->multiout.num_dacs = 1;
	spec->multiout.dac_nids = stac9200_dac_nids;
	spec->adc_nids = stac9200_adc_nids;
	spec->mux_nids = stac9200_mux_nids;
	spec->num_muxes = 1;

	spec->init = stac9200_core_init;
	spec->mixer = stac9200_mixer;

	err = stac9200_parse_auto_config(codec);
	if (err < 0) {
		stac92xx_free(codec);
		return err;
	}

	codec->patch_ops = stac92xx_patch_ops;

	return 0;
}

static int patch_stac922x(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	spec->board_config = snd_hda_check_board_config(codec, stac922x_cfg_tbl);
	if (spec->board_config < 0)
                snd_printdd(KERN_INFO "hda_codec: Unknown model for STAC922x, using BIOS defaults\n");
	else {
		spec->num_pins = 10;
		spec->pin_nids = stac922x_pin_nids;
		spec->pin_configs = stac922x_brd_tbl[spec->board_config];
		stac92xx_set_config_regs(codec);
	}

	spec->adc_nids = stac922x_adc_nids;
	spec->mux_nids = stac922x_mux_nids;
	spec->num_muxes = 2;

	spec->init = stac922x_core_init;
	spec->mixer = stac922x_mixer;

	spec->multiout.dac_nids = spec->dac_nids;

	err = stac92xx_parse_auto_config(codec, 0x08, 0x09);
	if (err < 0) {
		stac92xx_free(codec);
		return err;
	}

	codec->patch_ops = stac92xx_patch_ops;

	return 0;
}

static int patch_stac927x(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	spec->board_config = snd_hda_check_board_config(codec, stac927x_cfg_tbl);
	if (spec->board_config < 0)
                snd_printdd(KERN_INFO "hda_codec: Unknown model for STAC927x, using BIOS defaults\n");
	else {
		spec->num_pins = 14;
		spec->pin_nids = stac927x_pin_nids;
		spec->pin_configs = stac927x_brd_tbl[spec->board_config];
		stac92xx_set_config_regs(codec);
	}

	spec->adc_nids = stac927x_adc_nids;
	spec->mux_nids = stac927x_mux_nids;
	spec->num_muxes = 3;

	spec->init = stac927x_core_init;
	spec->mixer = stac927x_mixer;

	spec->multiout.dac_nids = spec->dac_nids;

	err = stac92xx_parse_auto_config(codec, 0x1e, 0x20);
	if (err < 0) {
		stac92xx_free(codec);
		return err;
	}

	codec->patch_ops = stac92xx_patch_ops;

	return 0;
}

/*
 * patch entries
 */
struct hda_codec_preset snd_hda_preset_sigmatel[] = {
 	{ .id = 0x83847690, .name = "STAC9200", .patch = patch_stac9200 },
 	{ .id = 0x83847882, .name = "STAC9220 A1", .patch = patch_stac922x },
 	{ .id = 0x83847680, .name = "STAC9221 A1", .patch = patch_stac922x },
 	{ .id = 0x83847880, .name = "STAC9220 A2", .patch = patch_stac922x },
 	{ .id = 0x83847681, .name = "STAC9220D/9223D A2", .patch = patch_stac922x },
 	{ .id = 0x83847682, .name = "STAC9221 A2", .patch = patch_stac922x },
 	{ .id = 0x83847683, .name = "STAC9221D A2", .patch = patch_stac922x },
 	{ .id = 0x83847620, .name = "STAC9274", .patch = patch_stac927x },
 	{ .id = 0x83847621, .name = "STAC9274D", .patch = patch_stac927x },
 	{ .id = 0x83847622, .name = "STAC9273X", .patch = patch_stac927x },
 	{ .id = 0x83847623, .name = "STAC9273D", .patch = patch_stac927x },
 	{ .id = 0x83847624, .name = "STAC9272X", .patch = patch_stac927x },
 	{ .id = 0x83847625, .name = "STAC9272D", .patch = patch_stac927x },
 	{ .id = 0x83847626, .name = "STAC9271X", .patch = patch_stac927x },
 	{ .id = 0x83847627, .name = "STAC9271D", .patch = patch_stac927x },
 	{ .id = 0x83847628, .name = "STAC9274X5NH", .patch = patch_stac927x },
 	{ .id = 0x83847629, .name = "STAC9274D5NH", .patch = patch_stac927x },
	{} /* terminator */
};
