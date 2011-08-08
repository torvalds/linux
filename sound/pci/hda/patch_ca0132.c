/*
 * HD audio interface patch for Creative CA0132 chip
 *
 * Copyright (c) 2011, Creative Technology Ltd.
 *
 * Based on patch_ca0110.c
 * Copyright (c) 2008 Takashi Iwai <tiwai@suse.de>
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

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"

#define WIDGET_CHIP_CTRL      0x15
#define WIDGET_DSP_CTRL       0x16

#define WUH_MEM_CONNID        10
#define DSP_MEM_CONNID        16

enum hda_cmd_vendor_io {
	/* for DspIO node */
	VENDOR_DSPIO_SCP_WRITE_DATA_LOW      = 0x000,
	VENDOR_DSPIO_SCP_WRITE_DATA_HIGH     = 0x100,

	VENDOR_DSPIO_STATUS                  = 0xF01,
	VENDOR_DSPIO_SCP_POST_READ_DATA      = 0x702,
	VENDOR_DSPIO_SCP_READ_DATA           = 0xF02,
	VENDOR_DSPIO_DSP_INIT                = 0x703,
	VENDOR_DSPIO_SCP_POST_COUNT_QUERY    = 0x704,
	VENDOR_DSPIO_SCP_READ_COUNT          = 0xF04,

	/* for ChipIO node */
	VENDOR_CHIPIO_ADDRESS_LOW            = 0x000,
	VENDOR_CHIPIO_ADDRESS_HIGH           = 0x100,
	VENDOR_CHIPIO_STREAM_FORMAT          = 0x200,
	VENDOR_CHIPIO_DATA_LOW               = 0x300,
	VENDOR_CHIPIO_DATA_HIGH              = 0x400,

	VENDOR_CHIPIO_GET_PARAMETER          = 0xF00,
	VENDOR_CHIPIO_STATUS                 = 0xF01,
	VENDOR_CHIPIO_HIC_POST_READ          = 0x702,
	VENDOR_CHIPIO_HIC_READ_DATA          = 0xF03,

	VENDOR_CHIPIO_CT_EXTENSIONS_ENABLE   = 0x70A,

	VENDOR_CHIPIO_PLL_PMU_WRITE          = 0x70C,
	VENDOR_CHIPIO_PLL_PMU_READ           = 0xF0C,
	VENDOR_CHIPIO_8051_ADDRESS_LOW       = 0x70D,
	VENDOR_CHIPIO_8051_ADDRESS_HIGH      = 0x70E,
	VENDOR_CHIPIO_FLAG_SET               = 0x70F,
	VENDOR_CHIPIO_FLAGS_GET              = 0xF0F,
	VENDOR_CHIPIO_PARAMETER_SET          = 0x710,
	VENDOR_CHIPIO_PARAMETER_GET          = 0xF10,

	VENDOR_CHIPIO_PORT_ALLOC_CONFIG_SET  = 0x711,
	VENDOR_CHIPIO_PORT_ALLOC_SET         = 0x712,
	VENDOR_CHIPIO_PORT_ALLOC_GET         = 0xF12,
	VENDOR_CHIPIO_PORT_FREE_SET          = 0x713,

	VENDOR_CHIPIO_PARAMETER_EX_ID_GET    = 0xF17,
	VENDOR_CHIPIO_PARAMETER_EX_ID_SET    = 0x717,
	VENDOR_CHIPIO_PARAMETER_EX_VALUE_GET = 0xF18,
	VENDOR_CHIPIO_PARAMETER_EX_VALUE_SET = 0x718
};

/*
 *  Control flag IDs
 */
enum control_flag_id {
	/* Connection manager stream setup is bypassed/enabled */
	CONTROL_FLAG_C_MGR                  = 0,
	/* DSP DMA is bypassed/enabled */
	CONTROL_FLAG_DMA                    = 1,
	/* 8051 'idle' mode is disabled/enabled */
	CONTROL_FLAG_IDLE_ENABLE            = 2,
	/* Tracker for the SPDIF-in path is bypassed/enabled */
	CONTROL_FLAG_TRACKER                = 3,
	/* DigitalOut to Spdif2Out connection is disabled/enabled */
	CONTROL_FLAG_SPDIF2OUT              = 4,
	/* Digital Microphone is disabled/enabled */
	CONTROL_FLAG_DMIC                   = 5,
	/* ADC_B rate is 48 kHz/96 kHz */
	CONTROL_FLAG_ADC_B_96KHZ            = 6,
	/* ADC_C rate is 48 kHz/96 kHz */
	CONTROL_FLAG_ADC_C_96KHZ            = 7,
	/* DAC rate is 48 kHz/96 kHz (affects all DACs) */
	CONTROL_FLAG_DAC_96KHZ              = 8,
	/* DSP rate is 48 kHz/96 kHz */
	CONTROL_FLAG_DSP_96KHZ              = 9,
	/* SRC clock is 98 MHz/196 MHz (196 MHz forces rate to 96 KHz) */
	CONTROL_FLAG_SRC_CLOCK_196MHZ       = 10,
	/* SRC rate is 48 kHz/96 kHz (48 kHz disabled when clock is 196 MHz) */
	CONTROL_FLAG_SRC_RATE_96KHZ         = 11,
	/* Decode Loop (DSP->SRC->DSP) is disabled/enabled */
	CONTROL_FLAG_DECODE_LOOP            = 12,
	/* De-emphasis filter on DAC-1 disabled/enabled */
	CONTROL_FLAG_DAC1_DEEMPHASIS        = 13,
	/* De-emphasis filter on DAC-2 disabled/enabled */
	CONTROL_FLAG_DAC2_DEEMPHASIS        = 14,
	/* De-emphasis filter on DAC-3 disabled/enabled */
	CONTROL_FLAG_DAC3_DEEMPHASIS        = 15,
	/* High-pass filter on ADC_B disabled/enabled */
	CONTROL_FLAG_ADC_B_HIGH_PASS        = 16,
	/* High-pass filter on ADC_C disabled/enabled */
	CONTROL_FLAG_ADC_C_HIGH_PASS        = 17,
	/* Common mode on Port_A disabled/enabled */
	CONTROL_FLAG_PORT_A_COMMON_MODE     = 18,
	/* Common mode on Port_D disabled/enabled */
	CONTROL_FLAG_PORT_D_COMMON_MODE     = 19,
	/* Impedance for ramp generator on Port_A 16 Ohm/10K Ohm */
	CONTROL_FLAG_PORT_A_10KOHM_LOAD     = 20,
	/* Impedance for ramp generator on Port_D, 16 Ohm/10K Ohm */
	CONTROL_FLAG_PORT_D_10K0HM_LOAD     = 21,
	/* ASI rate is 48kHz/96kHz */
	CONTROL_FLAG_ASI_96KHZ              = 22,
	/* DAC power settings able to control attached ports no/yes */
	CONTROL_FLAG_DACS_CONTROL_PORTS     = 23,
	/* Clock Stop OK reporting is disabled/enabled */
	CONTROL_FLAG_CONTROL_STOP_OK_ENABLE = 24,
	/* Number of control flags */
	CONTROL_FLAGS_MAX = (CONTROL_FLAG_CONTROL_STOP_OK_ENABLE+1)
};

/*
 * Control parameter IDs
 */
enum control_parameter_id {
	/* 0: force HDA, 1: allow DSP if HDA Spdif1Out stream is idle */
	CONTROL_PARAM_SPDIF1_SOURCE            = 2,

	/* Stream Control */

	/* Select stream with the given ID */
	CONTROL_PARAM_STREAM_ID                = 24,
	/* Source connection point for the selected stream */
	CONTROL_PARAM_STREAM_SOURCE_CONN_POINT = 25,
	/* Destination connection point for the selected stream */
	CONTROL_PARAM_STREAM_DEST_CONN_POINT   = 26,
	/* Number of audio channels in the selected stream */
	CONTROL_PARAM_STREAMS_CHANNELS         = 27,
	/*Enable control for the selected stream */
	CONTROL_PARAM_STREAM_CONTROL           = 28,

	/* Connection Point Control */

	/* Select connection point with the given ID */
	CONTROL_PARAM_CONN_POINT_ID            = 29,
	/* Connection point sample rate */
	CONTROL_PARAM_CONN_POINT_SAMPLE_RATE   = 30,

	/* Node Control */

	/* Select HDA node with the given ID */
	CONTROL_PARAM_NODE_ID                  = 31
};

/*
 *  Dsp Io Status codes
 */
enum hda_vendor_status_dspio {
	/* Success */
	VENDOR_STATUS_DSPIO_OK                       = 0x00,
	/* Busy, unable to accept new command, the host must retry */
	VENDOR_STATUS_DSPIO_BUSY                     = 0x01,
	/* SCP command queue is full */
	VENDOR_STATUS_DSPIO_SCP_COMMAND_QUEUE_FULL   = 0x02,
	/* SCP response queue is empty */
	VENDOR_STATUS_DSPIO_SCP_RESPONSE_QUEUE_EMPTY = 0x03
};

/*
 *  Chip Io Status codes
 */
enum hda_vendor_status_chipio {
	/* Success */
	VENDOR_STATUS_CHIPIO_OK   = 0x00,
	/* Busy, unable to accept new command, the host must retry */
	VENDOR_STATUS_CHIPIO_BUSY = 0x01
};

/*
 *  CA0132 sample rate
 */
enum ca0132_sample_rate {
	SR_6_000        = 0x00,
	SR_8_000        = 0x01,
	SR_9_600        = 0x02,
	SR_11_025       = 0x03,
	SR_16_000       = 0x04,
	SR_22_050       = 0x05,
	SR_24_000       = 0x06,
	SR_32_000       = 0x07,
	SR_44_100       = 0x08,
	SR_48_000       = 0x09,
	SR_88_200       = 0x0A,
	SR_96_000       = 0x0B,
	SR_144_000      = 0x0C,
	SR_176_400      = 0x0D,
	SR_192_000      = 0x0E,
	SR_384_000      = 0x0F,

	SR_COUNT        = 0x10,

	SR_RATE_UNKNOWN = 0x1F
};

/*
 *  Scp Helper function
 */
enum get_set {
	IS_SET = 0,
	IS_GET = 1,
};

/*
 * Duplicated from ca0110 codec
 */

static void init_output(struct hda_codec *codec, hda_nid_t pin, hda_nid_t dac)
{
	if (pin) {
		snd_hda_codec_write(codec, pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP);
		if (get_wcaps(codec, pin) & AC_WCAP_OUT_AMP)
			snd_hda_codec_write(codec, pin, 0,
					    AC_VERB_SET_AMP_GAIN_MUTE,
					    AMP_OUT_UNMUTE);
	}
	if (dac)
		snd_hda_codec_write(codec, dac, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO);
}

static void init_input(struct hda_codec *codec, hda_nid_t pin, hda_nid_t adc)
{
	if (pin) {
		snd_hda_codec_write(codec, pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL,
				    PIN_VREF80);
		if (get_wcaps(codec, pin) & AC_WCAP_IN_AMP)
			snd_hda_codec_write(codec, pin, 0,
					    AC_VERB_SET_AMP_GAIN_MUTE,
					    AMP_IN_UNMUTE(0));
	}
	if (adc)
		snd_hda_codec_write(codec, adc, 0, AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_IN_UNMUTE(0));
}

static char *dirstr[2] = { "Playback", "Capture" };

static int _add_switch(struct hda_codec *codec, hda_nid_t nid, const char *pfx,
		       int chan, int dir)
{
	char namestr[44];
	int type = dir ? HDA_INPUT : HDA_OUTPUT;
	struct snd_kcontrol_new knew =
		HDA_CODEC_MUTE_MONO(namestr, nid, chan, 0, type);
	sprintf(namestr, "%s %s Switch", pfx, dirstr[dir]);
	return snd_hda_ctl_add(codec, nid, snd_ctl_new1(&knew, codec));
}

static int _add_volume(struct hda_codec *codec, hda_nid_t nid, const char *pfx,
		       int chan, int dir)
{
	char namestr[44];
	int type = dir ? HDA_INPUT : HDA_OUTPUT;
	struct snd_kcontrol_new knew =
		HDA_CODEC_VOLUME_MONO(namestr, nid, chan, 0, type);
	sprintf(namestr, "%s %s Volume", pfx, dirstr[dir]);
	return snd_hda_ctl_add(codec, nid, snd_ctl_new1(&knew, codec));
}

#define add_out_switch(codec, nid, pfx) _add_switch(codec, nid, pfx, 3, 0)
#define add_out_volume(codec, nid, pfx) _add_volume(codec, nid, pfx, 3, 0)
#define add_in_switch(codec, nid, pfx) _add_switch(codec, nid, pfx, 3, 1)
#define add_in_volume(codec, nid, pfx) _add_volume(codec, nid, pfx, 3, 1)
#define add_mono_switch(codec, nid, pfx, chan) \
	_add_switch(codec, nid, pfx, chan, 0)
#define add_mono_volume(codec, nid, pfx, chan) \
	_add_volume(codec, nid, pfx, chan, 0)
#define add_in_mono_switch(codec, nid, pfx, chan) \
	_add_switch(codec, nid, pfx, chan, 1)
#define add_in_mono_volume(codec, nid, pfx, chan) \
	_add_volume(codec, nid, pfx, chan, 1)


/*
 * CA0132 specific
 */

struct ca0132_spec {
	struct auto_pin_cfg autocfg;
	struct hda_multi_out multiout;
	hda_nid_t out_pins[AUTO_CFG_MAX_OUTS];
	hda_nid_t dacs[AUTO_CFG_MAX_OUTS];
	hda_nid_t hp_dac;
	hda_nid_t input_pins[AUTO_PIN_LAST];
	hda_nid_t adcs[AUTO_PIN_LAST];
	hda_nid_t dig_out;
	hda_nid_t dig_in;
	unsigned int num_inputs;
	long curr_hp_switch;
	long curr_hp_volume[2];
	long curr_speaker_switch;
	struct mutex chipio_mutex;
	const char *input_labels[AUTO_PIN_LAST];
	struct hda_pcm pcm_rec[2]; /* PCM information */
};

/* Chip access helper function */
static int chipio_send(struct hda_codec *codec,
		       unsigned int reg,
		       unsigned int data)
{
	unsigned int res;
	int retry = 50;

	/* send bits of data specified by reg */
	do {
		res = snd_hda_codec_read(codec, WIDGET_CHIP_CTRL, 0,
					 reg, data);
		if (res == VENDOR_STATUS_CHIPIO_OK)
			return 0;
	} while (--retry);
	return -EIO;
}

/*
 * Write chip address through the vendor widget -- NOT protected by the Mutex!
 */
static int chipio_write_address(struct hda_codec *codec,
				unsigned int chip_addx)
{
	int res;

	/* send low 16 bits of the address */
	res = chipio_send(codec, VENDOR_CHIPIO_ADDRESS_LOW,
			  chip_addx & 0xffff);

	if (res != -EIO) {
		/* send high 16 bits of the address */
		res = chipio_send(codec, VENDOR_CHIPIO_ADDRESS_HIGH,
				  chip_addx >> 16);
	}

	return res;
}

/*
 * Write data through the vendor widget -- NOT protected by the Mutex!
 */

static int chipio_write_data(struct hda_codec *codec, unsigned int data)
{
	int res;

	/* send low 16 bits of the data */
	res = chipio_send(codec, VENDOR_CHIPIO_DATA_LOW, data & 0xffff);

	if (res != -EIO) {
		/* send high 16 bits of the data */
		res = chipio_send(codec, VENDOR_CHIPIO_DATA_HIGH,
				  data >> 16);
	}

	return res;
}

/*
 * Read data through the vendor widget -- NOT protected by the Mutex!
 */
static int chipio_read_data(struct hda_codec *codec, unsigned int *data)
{
	int res;

	/* post read */
	res = chipio_send(codec, VENDOR_CHIPIO_HIC_POST_READ, 0);

	if (res != -EIO) {
		/* read status */
		res = chipio_send(codec, VENDOR_CHIPIO_STATUS, 0);
	}

	if (res != -EIO) {
		/* read data */
		*data = snd_hda_codec_read(codec, WIDGET_CHIP_CTRL, 0,
					   VENDOR_CHIPIO_HIC_READ_DATA,
					   0);
	}

	return res;
}

/*
 * Write given value to the given address through the chip I/O widget.
 * protected by the Mutex
 */
static int chipio_write(struct hda_codec *codec,
		unsigned int chip_addx, const unsigned int data)
{
	struct ca0132_spec *spec = codec->spec;
	int err;

	mutex_lock(&spec->chipio_mutex);

	/* write the address, and if successful proceed to write data */
	err = chipio_write_address(codec, chip_addx);
	if (err < 0)
		goto exit;

	err = chipio_write_data(codec, data);
	if (err < 0)
		goto exit;

exit:
	mutex_unlock(&spec->chipio_mutex);
	return err;
}

/*
 * Read the given address through the chip I/O widget
 * protected by the Mutex
 */
static int chipio_read(struct hda_codec *codec,
		unsigned int chip_addx, unsigned int *data)
{
	struct ca0132_spec *spec = codec->spec;
	int err;

	mutex_lock(&spec->chipio_mutex);

	/* write the address, and if successful proceed to write data */
	err = chipio_write_address(codec, chip_addx);
	if (err < 0)
		goto exit;

	err = chipio_read_data(codec, data);
	if (err < 0)
		goto exit;

exit:
	mutex_unlock(&spec->chipio_mutex);
	return err;
}

/*
 * PCM stuffs
 */
static void ca0132_setup_stream(struct hda_codec *codec, hda_nid_t nid,
				 u32 stream_tag,
				 int channel_id, int format)
{
	unsigned int oldval, newval;

	if (!nid)
		return;

	snd_printdd("ca0132_setup_stream: "
		"NID=0x%x, stream=0x%x, channel=%d, format=0x%x\n",
		nid, stream_tag, channel_id, format);

	/* update the format-id if changed */
	oldval = snd_hda_codec_read(codec, nid, 0,
				    AC_VERB_GET_STREAM_FORMAT,
				    0);
	if (oldval != format) {
		msleep(20);
		snd_hda_codec_write(codec, nid, 0,
				    AC_VERB_SET_STREAM_FORMAT,
				    format);
	}

	oldval = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_CONV, 0);
	newval = (stream_tag << 4) | channel_id;
	if (oldval != newval) {
		snd_hda_codec_write(codec, nid, 0,
				    AC_VERB_SET_CHANNEL_STREAMID,
				    newval);
	}
}

static void ca0132_cleanup_stream(struct hda_codec *codec, hda_nid_t nid)
{
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_STREAM_FORMAT, 0);
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_CHANNEL_STREAMID, 0);
}

/*
 * PCM callbacks
 */
static int ca0132_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
			struct hda_codec *codec,
			unsigned int stream_tag,
			unsigned int format,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;

	ca0132_setup_stream(codec, spec->dacs[0], stream_tag, 0, format);

	return 0;
}

static int ca0132_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
			struct hda_codec *codec,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;

	ca0132_cleanup_stream(codec, spec->dacs[0]);

	return 0;
}

/*
 * Digital out
 */
static int ca0132_dig_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
			struct hda_codec *codec,
			unsigned int stream_tag,
			unsigned int format,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;

	ca0132_setup_stream(codec, spec->dig_out, stream_tag, 0, format);

	return 0;
}

static int ca0132_dig_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
			struct hda_codec *codec,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;

	ca0132_cleanup_stream(codec, spec->dig_out);

	return 0;
}

/*
 * Analog capture
 */
static int ca0132_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
			struct hda_codec *codec,
			unsigned int stream_tag,
			unsigned int format,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;

	ca0132_setup_stream(codec, spec->adcs[substream->number],
			     stream_tag, 0, format);

	return 0;
}

static int ca0132_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
			struct hda_codec *codec,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;

	ca0132_cleanup_stream(codec, spec->adcs[substream->number]);

	return 0;
}

/*
 * Digital capture
 */
static int ca0132_dig_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
			struct hda_codec *codec,
			unsigned int stream_tag,
			unsigned int format,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;

	ca0132_setup_stream(codec, spec->dig_in, stream_tag, 0, format);

	return 0;
}

static int ca0132_dig_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
			struct hda_codec *codec,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;

	ca0132_cleanup_stream(codec, spec->dig_in);

	return 0;
}

/*
 */
static struct hda_pcm_stream ca0132_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.ops = {
		.prepare = ca0132_playback_pcm_prepare,
		.cleanup = ca0132_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream ca0132_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.ops = {
		.prepare = ca0132_capture_pcm_prepare,
		.cleanup = ca0132_capture_pcm_cleanup
	},
};

static struct hda_pcm_stream ca0132_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.ops = {
		.prepare = ca0132_dig_playback_pcm_prepare,
		.cleanup = ca0132_dig_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream ca0132_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.ops = {
		.prepare = ca0132_dig_capture_pcm_prepare,
		.cleanup = ca0132_dig_capture_pcm_cleanup
	},
};

static int ca0132_build_pcms(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;

	codec->pcm_info = info;
	codec->num_pcms = 0;

	info->name = "CA0132 Analog";
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = ca0132_pcm_analog_playback;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->dacs[0];
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max =
		spec->multiout.max_channels;
	info->stream[SNDRV_PCM_STREAM_CAPTURE] = ca0132_pcm_analog_capture;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].substreams = spec->num_inputs;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adcs[0];
	codec->num_pcms++;

	if (!spec->dig_out && !spec->dig_in)
		return 0;

	info++;
	info->name = "CA0132 Digital";
	info->pcm_type = HDA_PCM_TYPE_SPDIF;
	if (spec->dig_out) {
		info->stream[SNDRV_PCM_STREAM_PLAYBACK] =
			ca0132_pcm_digital_playback;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->dig_out;
	}
	if (spec->dig_in) {
		info->stream[SNDRV_PCM_STREAM_CAPTURE] =
			ca0132_pcm_digital_capture;
		info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->dig_in;
	}
	codec->num_pcms++;

	return 0;
}

#define REG_CODEC_MUTE		0x18b014
#define REG_CODEC_HP_VOL_L	0x18b070
#define REG_CODEC_HP_VOL_R	0x18b074

static int ca0132_hp_switch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	long *valp = ucontrol->value.integer.value;

	*valp = spec->curr_hp_switch;
	return 0;
}

static int ca0132_hp_switch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	long *valp = ucontrol->value.integer.value;
	unsigned int data;
	int err;

	/* any change? */
	if (spec->curr_hp_switch == *valp)
		return 0;

	snd_hda_power_up(codec);

	err = chipio_read(codec, REG_CODEC_MUTE, &data);
	if (err < 0)
		return err;

	/* *valp 0 is mute, 1 is unmute */
	data = (data & 0x7f) | (*valp ? 0 : 0x80);
	chipio_write(codec, REG_CODEC_MUTE, data);
	if (err < 0)
		return err;

	spec->curr_hp_switch = *valp;

	snd_hda_power_down(codec);
	return 1;
}

static int ca0132_speaker_switch_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	long *valp = ucontrol->value.integer.value;

	*valp = spec->curr_speaker_switch;
	return 0;
}

static int ca0132_speaker_switch_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	long *valp = ucontrol->value.integer.value;
	unsigned int data;
	int err;

	/* any change? */
	if (spec->curr_speaker_switch == *valp)
		return 0;

	snd_hda_power_up(codec);

	err = chipio_read(codec, REG_CODEC_MUTE, &data);
	if (err < 0)
		return err;

	/* *valp 0 is mute, 1 is unmute */
	data = (data & 0xef) | (*valp ? 0 : 0x10);
	chipio_write(codec, REG_CODEC_MUTE, data);
	if (err < 0)
		return err;

	spec->curr_speaker_switch = *valp;

	snd_hda_power_down(codec);
	return 1;
}

static int ca0132_hp_volume_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	long *valp = ucontrol->value.integer.value;

	*valp++ = spec->curr_hp_volume[0];
	*valp = spec->curr_hp_volume[1];
	return 0;
}

static int ca0132_hp_volume_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	long *valp = ucontrol->value.integer.value;
	long left_vol, right_vol;
	unsigned int data;
	int val;
	int err;

	left_vol = *valp++;
	right_vol = *valp;

	/* any change? */
	if ((spec->curr_hp_volume[0] == left_vol) &&
		(spec->curr_hp_volume[1] == right_vol))
		return 0;

	snd_hda_power_up(codec);

	err = chipio_read(codec, REG_CODEC_HP_VOL_L, &data);
	if (err < 0)
		return err;

	val = 31 - left_vol;
	data = (data & 0xe0) | val;
	chipio_write(codec, REG_CODEC_HP_VOL_L, data);
	if (err < 0)
		return err;

	val = 31 - right_vol;
	data = (data & 0xe0) | val;
	chipio_write(codec, REG_CODEC_HP_VOL_R, data);
	if (err < 0)
		return err;

	spec->curr_hp_volume[0] = left_vol;
	spec->curr_hp_volume[1] = right_vol;

	snd_hda_power_down(codec);
	return 1;
}

static int add_hp_switch(struct hda_codec *codec, hda_nid_t nid)
{
	struct snd_kcontrol_new knew =
		HDA_CODEC_MUTE_MONO("Headphone Playback Switch",
				     nid, 1, 0, HDA_OUTPUT);
	knew.get = ca0132_hp_switch_get;
	knew.put = ca0132_hp_switch_put;
	return snd_hda_ctl_add(codec, nid, snd_ctl_new1(&knew, codec));
}

static int add_hp_volume(struct hda_codec *codec, hda_nid_t nid)
{
	struct snd_kcontrol_new knew =
		HDA_CODEC_VOLUME_MONO("Headphone Playback Volume",
				       nid, 3, 0, HDA_OUTPUT);
	knew.get = ca0132_hp_volume_get;
	knew.put = ca0132_hp_volume_put;
	return snd_hda_ctl_add(codec, nid, snd_ctl_new1(&knew, codec));
}

static int add_speaker_switch(struct hda_codec *codec, hda_nid_t nid)
{
	struct snd_kcontrol_new knew =
		HDA_CODEC_MUTE_MONO("Speaker Playback Switch",
				     nid, 1, 0, HDA_OUTPUT);
	knew.get = ca0132_speaker_switch_get;
	knew.put = ca0132_speaker_switch_put;
	return snd_hda_ctl_add(codec, nid, snd_ctl_new1(&knew, codec));
}

static void ca0132_fix_hp_caps(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int caps;

	/* set mute-capable, 1db step, 32 steps, ofs 6 */
	caps = 0x80031f06;
	snd_hda_override_amp_caps(codec, cfg->hp_pins[0], HDA_OUTPUT, caps);
}

static int ca0132_build_controls(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i, err;

	if (spec->multiout.num_dacs) {
		err = add_speaker_switch(codec, spec->out_pins[0]);
		if (err < 0)
			return err;
	}

	if (cfg->hp_outs) {
		ca0132_fix_hp_caps(codec);
		err = add_hp_switch(codec, cfg->hp_pins[0]);
		if (err < 0)
			return err;
		err = add_hp_volume(codec, cfg->hp_pins[0]);
		if (err < 0)
			return err;
	}

	for (i = 0; i < spec->num_inputs; i++) {
		const char *label = spec->input_labels[i];

		err = add_in_switch(codec, spec->adcs[i], label);
		if (err < 0)
			return err;
		err = add_in_volume(codec, spec->adcs[i], label);
		if (err < 0)
			return err;
		if (cfg->inputs[i].type == AUTO_PIN_MIC) {
			/* add Mic-Boost */
			err = add_in_mono_volume(codec, spec->input_pins[i],
						 "Mic Boost", 1);
			if (err < 0)
				return err;
		}
	}

	if (spec->dig_out) {
		err = snd_hda_create_spdif_out_ctls(codec, spec->dig_out,
						    spec->dig_out);
		if (err < 0)
			return err;
		err = add_out_volume(codec, spec->dig_out, "IEC958");
		if (err < 0)
			return err;
	}

	if (spec->dig_in) {
		err = snd_hda_create_spdif_in_ctls(codec, spec->dig_in);
		if (err < 0)
			return err;
		err = add_in_volume(codec, spec->dig_in, "IEC958");
	}
	return 0;
}


static void ca0132_set_ct_ext(struct hda_codec *codec, int enable)
{
	/* Set Creative extension */
	snd_printdd("SET CREATIVE EXTENSION\n");
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_CT_EXTENSIONS_ENABLE,
			    enable);
	msleep(20);
}


static void ca0132_config(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;

	/* line-outs */
	cfg->line_outs = 1;
	cfg->line_out_pins[0] = 0x0b; /* front */
	cfg->line_out_type = AUTO_PIN_LINE_OUT;

	spec->dacs[0] = 0x02;
	spec->out_pins[0] = 0x0b;
	spec->multiout.dac_nids = spec->dacs;
	spec->multiout.num_dacs = 1;
	spec->multiout.max_channels = 2;

	/* headphone */
	cfg->hp_outs = 1;
	cfg->hp_pins[0] = 0x0f;

	spec->hp_dac = 0;
	spec->multiout.hp_nid = 0;

	/* inputs */
	cfg->num_inputs = 2;  /* Mic-in and line-in */
	cfg->inputs[0].pin = 0x12;
	cfg->inputs[0].type = AUTO_PIN_MIC;
	cfg->inputs[1].pin = 0x11;
	cfg->inputs[1].type = AUTO_PIN_LINE_IN;

	/* Mic-in */
	spec->input_pins[0] = 0x12;
	spec->input_labels[0] = "Mic-In";
	spec->adcs[0] = 0x07;

	/* Line-In */
	spec->input_pins[1] = 0x11;
	spec->input_labels[1] = "Line-In";
	spec->adcs[1] = 0x08;
	spec->num_inputs = 2;
}

static void ca0132_init_chip(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	mutex_init(&spec->chipio_mutex);
}

static void ca0132_exit_chip(struct hda_codec *codec)
{
	/* put any chip cleanup stuffs here. */
}

static int ca0132_init(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	for (i = 0; i < spec->multiout.num_dacs; i++) {
		init_output(codec, spec->out_pins[i],
			    spec->multiout.dac_nids[i]);
	}
	init_output(codec, cfg->hp_pins[0], spec->hp_dac);
	init_output(codec, cfg->dig_out_pins[0], spec->dig_out);

	for (i = 0; i < spec->num_inputs; i++)
		init_input(codec, spec->input_pins[i], spec->adcs[i]);

	init_input(codec, cfg->dig_in_pin, spec->dig_in);

	ca0132_set_ct_ext(codec, 1);

	return 0;
}


static void ca0132_free(struct hda_codec *codec)
{
	ca0132_set_ct_ext(codec, 0);
	ca0132_exit_chip(codec);
	kfree(codec->spec);
}

static struct hda_codec_ops ca0132_patch_ops = {
	.build_controls = ca0132_build_controls,
	.build_pcms = ca0132_build_pcms,
	.init = ca0132_init,
	.free = ca0132_free,
};



static int patch_ca0132(struct hda_codec *codec)
{
	struct ca0132_spec *spec;

	snd_printdd("patch_ca0132\n");

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	codec->spec = spec;

	ca0132_init_chip(codec);

	ca0132_config(codec);

	codec->patch_ops = ca0132_patch_ops;

	return 0;
}

/*
 * patch entries
 */
static struct hda_codec_preset snd_hda_preset_ca0132[] = {
	{ .id = 0x11020011, .name = "CA0132",     .patch = patch_ca0132 },
	{} /* terminator */
};

MODULE_ALIAS("snd-hda-codec-id:11020011");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Creative CA0132, CA0132 HD-audio codec");

static struct hda_codec_preset_list ca0132_list = {
	.preset = snd_hda_preset_ca0132,
	.owner = THIS_MODULE,
};

static int __init patch_ca0132_init(void)
{
	return snd_hda_add_codec_preset(&ca0132_list);
}

static void __exit patch_ca0132_exit(void)
{
	snd_hda_delete_codec_preset(&ca0132_list);
}

module_init(patch_ca0132_init)
module_exit(patch_ca0132_exit)
