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
#include <linux/module.h>
#include <linux/firmware.h>
#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_auto_parser.h"

#include "ca0132_regs.h"

#define DSP_DMA_WRITE_BUFLEN_INIT (1UL<<18)
#define DSP_DMA_WRITE_BUFLEN_OVLY (1UL<<15)

#define DMA_TRANSFER_FRAME_SIZE_NWORDS		8
#define DMA_TRANSFER_MAX_FRAME_SIZE_NWORDS	32
#define DMA_OVERLAY_FRAME_SIZE_NWORDS		2

#define MASTERCONTROL				0x80
#define MASTERCONTROL_ALLOC_DMA_CHAN		9

#define WIDGET_CHIP_CTRL      0x15
#define WIDGET_DSP_CTRL       0x16

#define MEM_CONNID_MICIN1     3
#define MEM_CONNID_MICIN2     5
#define MEM_CONNID_MICOUT1    12
#define MEM_CONNID_MICOUT2    14
#define MEM_CONNID_WUH        10
#define MEM_CONNID_DSP        16
#define MEM_CONNID_DMIC       100

#define SCP_SET    0
#define SCP_GET    1

#define EFX_FILE   "ctefx.bin"

MODULE_FIRMWARE(EFX_FILE);

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

	VENDOR_CHIPIO_8051_DATA_WRITE        = 0x707,
	VENDOR_CHIPIO_8051_DATA_READ         = 0xF07,

	VENDOR_CHIPIO_CT_EXTENSIONS_ENABLE   = 0x70A,
	VENDOR_CHIPIO_CT_EXTENSIONS_GET      = 0xF0A,

	VENDOR_CHIPIO_PLL_PMU_WRITE          = 0x70C,
	VENDOR_CHIPIO_PLL_PMU_READ           = 0xF0C,
	VENDOR_CHIPIO_8051_ADDRESS_LOW       = 0x70D,
	VENDOR_CHIPIO_8051_ADDRESS_HIGH      = 0x70E,
	VENDOR_CHIPIO_FLAG_SET               = 0x70F,
	VENDOR_CHIPIO_FLAGS_GET              = 0xF0F,
	VENDOR_CHIPIO_PARAM_SET              = 0x710,
	VENDOR_CHIPIO_PARAM_GET              = 0xF10,

	VENDOR_CHIPIO_PORT_ALLOC_CONFIG_SET  = 0x711,
	VENDOR_CHIPIO_PORT_ALLOC_SET         = 0x712,
	VENDOR_CHIPIO_PORT_ALLOC_GET         = 0xF12,
	VENDOR_CHIPIO_PORT_FREE_SET          = 0x713,

	VENDOR_CHIPIO_PARAM_EX_ID_GET        = 0xF17,
	VENDOR_CHIPIO_PARAM_EX_ID_SET        = 0x717,
	VENDOR_CHIPIO_PARAM_EX_VALUE_GET     = 0xF18,
	VENDOR_CHIPIO_PARAM_EX_VALUE_SET     = 0x718,

	VENDOR_CHIPIO_DMIC_CTL_SET           = 0x788,
	VENDOR_CHIPIO_DMIC_CTL_GET           = 0xF88,
	VENDOR_CHIPIO_DMIC_PIN_SET           = 0x789,
	VENDOR_CHIPIO_DMIC_PIN_GET           = 0xF89,
	VENDOR_CHIPIO_DMIC_MCLK_SET          = 0x78A,
	VENDOR_CHIPIO_DMIC_MCLK_GET          = 0xF8A,

	VENDOR_CHIPIO_EAPD_SEL_SET           = 0x78D
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
	CONTROL_FLAG_PORT_D_10KOHM_LOAD     = 21,
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
enum control_param_id {
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
		snd_hda_set_pin_ctl(codec, pin, PIN_HP);
		if (get_wcaps(codec, pin) & AC_WCAP_OUT_AMP)
			snd_hda_codec_write(codec, pin, 0,
					    AC_VERB_SET_AMP_GAIN_MUTE,
					    AMP_OUT_UNMUTE);
	}
	if (dac && (get_wcaps(codec, dac) & AC_WCAP_OUT_AMP))
		snd_hda_codec_write(codec, dac, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO);
}

static void init_input(struct hda_codec *codec, hda_nid_t pin, hda_nid_t adc)
{
	if (pin) {
		snd_hda_set_pin_ctl(codec, pin, PIN_IN |
				    snd_hda_get_default_vref(codec, pin));
		if (get_wcaps(codec, pin) & AC_WCAP_IN_AMP)
			snd_hda_codec_write(codec, pin, 0,
					    AC_VERB_SET_AMP_GAIN_MUTE,
					    AMP_IN_UNMUTE(0));
	}
	if (adc && (get_wcaps(codec, adc) & AC_WCAP_IN_AMP))
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
	if ((query_amp_caps(codec, nid, type) & AC_AMPCAP_MUTE) == 0) {
		snd_printdd("Skipping '%s %s Switch' (no mute on node 0x%x)\n", pfx, dirstr[dir], nid);
		return 0;
	}
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
	if ((query_amp_caps(codec, nid, type) & AC_AMPCAP_NUM_STEPS) == 0) {
		snd_printdd("Skipping '%s %s Volume' (no amp on node 0x%x)\n", pfx, dirstr[dir], nid);
		return 0;
	}
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

enum dsp_download_state {
	DSP_DOWNLOAD_FAILED = -1,
	DSP_DOWNLOAD_INIT   = 0,
	DSP_DOWNLOADING     = 1,
	DSP_DOWNLOADED      = 2
};

/* retrieve parameters from hda format */
#define get_hdafmt_chs(fmt)	(fmt & 0xf)
#define get_hdafmt_bits(fmt)	((fmt >> 4) & 0x7)
#define get_hdafmt_rate(fmt)	((fmt >> 8) & 0x7f)
#define get_hdafmt_type(fmt)	((fmt >> 15) & 0x1)

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
	const char *input_labels[AUTO_PIN_LAST];
	struct hda_pcm pcm_rec[2]; /* PCM information */

	/* chip access */
	struct mutex chipio_mutex; /* chip access mutex */
	u32 curr_chip_addx;

	/* DSP download related */
	enum dsp_download_state dsp_state;
	unsigned int dsp_stream_id;
	unsigned int wait_scp;
	unsigned int wait_scp_header;
	unsigned int wait_num_data;
	unsigned int scp_resp_header;
	unsigned int scp_resp_data[4];
	unsigned int scp_resp_count;
};

/*
 * CA0132 codec access
 */
unsigned int codec_send_command(struct hda_codec *codec, hda_nid_t nid,
		unsigned int verb, unsigned int parm, unsigned int *res)
{
	unsigned int response;
	response = snd_hda_codec_read(codec, nid, 0, verb, parm);
	*res = response;

	return ((response == -1) ? -1 : 0);
}

static int codec_set_converter_format(struct hda_codec *codec, hda_nid_t nid,
		unsigned short converter_format, unsigned int *res)
{
	return codec_send_command(codec, nid, VENDOR_CHIPIO_STREAM_FORMAT,
				converter_format & 0xffff, res);
}

static int codec_set_converter_stream_channel(struct hda_codec *codec,
				hda_nid_t nid, unsigned char stream,
				unsigned char channel, unsigned int *res)
{
	unsigned char converter_stream_channel = 0;

	converter_stream_channel = (stream << 4) | (channel & 0x0f);
	return codec_send_command(codec, nid, AC_VERB_SET_CHANNEL_STREAMID,
				converter_stream_channel, res);
}

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
	struct ca0132_spec *spec = codec->spec;
	int res;

	if (spec->curr_chip_addx == chip_addx)
			return 0;

	/* send low 16 bits of the address */
	res = chipio_send(codec, VENDOR_CHIPIO_ADDRESS_LOW,
			  chip_addx & 0xffff);

	if (res != -EIO) {
		/* send high 16 bits of the address */
		res = chipio_send(codec, VENDOR_CHIPIO_ADDRESS_HIGH,
				  chip_addx >> 16);
	}

	spec->curr_chip_addx = (res < 0) ? ~0UL : chip_addx;

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
 * Write multiple data through the vendor widget -- NOT protected by the Mutex!
 */
static int chipio_write_data_multiple(struct hda_codec *codec,
				      const u32 *data,
				      unsigned int count)
{
	int status = 0;

	if (data == NULL) {
		snd_printdd(KERN_ERR "chipio_write_data null ptr");
		return -EINVAL;
	}

	while ((count-- != 0) && (status == 0))
		status = chipio_write_data(codec, *data++);

	return status;
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
 * Write multiple values to the given address through the chip I/O widget.
 * protected by the Mutex
 */
static int chipio_write_multiple(struct hda_codec *codec,
				 u32 chip_addx,
				 const u32 *data,
				 unsigned int count)
{
	struct ca0132_spec *spec = codec->spec;
	int status;

	mutex_lock(&spec->chipio_mutex);
	status = chipio_write_address(codec, chip_addx);
	if (status < 0)
		goto error;

	status = chipio_write_data_multiple(codec, data, count);
error:
	mutex_unlock(&spec->chipio_mutex);

	return status;
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
 * Set chip control flags through the chip I/O widget.
 */
static void chipio_set_control_flag(struct hda_codec *codec,
				    enum control_flag_id flag_id,
				    bool flag_state)
{
	unsigned int val;
	unsigned int flag_bit;

	flag_bit = (flag_state ? 1 : 0);
	val = (flag_bit << 7) | (flag_id);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_FLAG_SET, val);
}

/*
 * Set chip parameters through the chip I/O widget.
 */
static void chipio_set_control_param(struct hda_codec *codec,
		enum control_param_id param_id, int param_val)
{
	struct ca0132_spec *spec = codec->spec;
	int val;

	if ((param_id < 32) && (param_val < 8)) {
		val = (param_val << 5) | (param_id);
		snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
				    VENDOR_CHIPIO_PARAM_SET, val);
	} else {
		mutex_lock(&spec->chipio_mutex);
		if (chipio_send(codec, VENDOR_CHIPIO_STATUS, 0) == 0) {
			snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
					    VENDOR_CHIPIO_PARAM_EX_ID_SET,
					    param_id);
			snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
					    VENDOR_CHIPIO_PARAM_EX_VALUE_SET,
					    param_val);
		}
		mutex_unlock(&spec->chipio_mutex);
	}
}

/*
 * Set sampling rate of the connection point.
 */
static void chipio_set_conn_rate(struct hda_codec *codec,
				int connid, enum ca0132_sample_rate rate)
{
	chipio_set_control_param(codec, CONTROL_PARAM_CONN_POINT_ID, connid);
	chipio_set_control_param(codec, CONTROL_PARAM_CONN_POINT_SAMPLE_RATE,
				 rate);
}

/*
 * Enable clocks.
 */
static void chipio_enable_clocks(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	mutex_lock(&spec->chipio_mutex);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 0);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PLL_PMU_WRITE, 0xff);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 5);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PLL_PMU_WRITE, 0x0b);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 6);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PLL_PMU_WRITE, 0xff);
	mutex_unlock(&spec->chipio_mutex);
}

/*
 * CA0132 DSP IO stuffs
 */
static int dspio_send(struct hda_codec *codec, unsigned int reg,
		      unsigned int data)
{
	unsigned int res;
	int retry = 50;

	/* send bits of data specified by reg to dsp */
	do {
		res = snd_hda_codec_read(codec, WIDGET_DSP_CTRL, 0, reg, data);
		if ((res >= 0) && (res != VENDOR_STATUS_DSPIO_BUSY))
			return res;
	} while (--retry);

	return -EIO;
}

/*
 * Wait for DSP to be ready for commands
 */
static void dspio_write_wait(struct hda_codec *codec)
{
	int status;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	do {
		status = snd_hda_codec_read(codec, WIDGET_DSP_CTRL, 0,
						VENDOR_DSPIO_STATUS, 0);
		if ((status == VENDOR_STATUS_DSPIO_OK) ||
		    (status == VENDOR_STATUS_DSPIO_SCP_RESPONSE_QUEUE_EMPTY))
			break;
		msleep(1);
	} while (time_before(jiffies, timeout));
}

/*
 * Write SCP data to DSP
 */
static int dspio_write(struct hda_codec *codec, unsigned int scp_data)
{
	struct ca0132_spec *spec = codec->spec;
	int status;

	dspio_write_wait(codec);

	mutex_lock(&spec->chipio_mutex);
	status = dspio_send(codec, VENDOR_DSPIO_SCP_WRITE_DATA_LOW,
			    scp_data & 0xffff);
	if (status < 0)
		goto error;

	status = dspio_send(codec, VENDOR_DSPIO_SCP_WRITE_DATA_HIGH,
				    scp_data >> 16);
	if (status < 0)
		goto error;

	/* OK, now check if the write itself has executed*/
	status = snd_hda_codec_read(codec, WIDGET_DSP_CTRL, 0,
				    VENDOR_DSPIO_STATUS, 0);
error:
	mutex_unlock(&spec->chipio_mutex);

	return (status == VENDOR_STATUS_DSPIO_SCP_COMMAND_QUEUE_FULL) ?
			-EIO : 0;
}

/*
 * Write multiple SCP data to DSP
 */
static int dspio_write_multiple(struct hda_codec *codec,
				unsigned int *buffer, unsigned int size)
{
	int status = 0;
	unsigned int count;

	if ((buffer == NULL))
		return -EINVAL;

	count = 0;
	while (count < size) {
		status = dspio_write(codec, *buffer++);
		if (status != 0)
			break;
		count++;
	}

	return status;
}

/*
 * Construct the SCP header using corresponding fields
 */
static inline unsigned int
make_scp_header(unsigned int target_id, unsigned int source_id,
		unsigned int get_flag, unsigned int req,
		unsigned int device_flag, unsigned int resp_flag,
		unsigned int error_flag, unsigned int data_size)
{
	unsigned int header = 0;

	header = (data_size & 0x1f) << 27;
	header |= (error_flag & 0x01) << 26;
	header |= (resp_flag & 0x01) << 25;
	header |= (device_flag & 0x01) << 24;
	header |= (req & 0x7f) << 17;
	header |= (get_flag & 0x01) << 16;
	header |= (source_id & 0xff) << 8;
	header |= target_id & 0xff;

	return header;
}

/*
 * Extract corresponding fields from SCP header
 */
static inline void
extract_scp_header(unsigned int header,
		   unsigned int *target_id, unsigned int *source_id,
		   unsigned int *get_flag, unsigned int *req,
		   unsigned int *device_flag, unsigned int *resp_flag,
		   unsigned int *error_flag, unsigned int *data_size)
{
	if (data_size)
		*data_size = (header >> 27) & 0x1f;
	if (error_flag)
		*error_flag = (header >> 26) & 0x01;
	if (resp_flag)
		*resp_flag = (header >> 25) & 0x01;
	if (device_flag)
		*device_flag = (header >> 24) & 0x01;
	if (req)
		*req = (header >> 17) & 0x7f;
	if (get_flag)
		*get_flag = (header >> 16) & 0x01;
	if (source_id)
		*source_id = (header >> 8) & 0xff;
	if (target_id)
		*target_id = header & 0xff;
}

#define SCP_MAX_DATA_WORDS  (16)

/* Structure to contain any SCP message */
struct scp_msg {
	unsigned int hdr;
	unsigned int data[SCP_MAX_DATA_WORDS];
};

/*
 * Send SCP message to DSP
 */
static int dspio_send_scp_message(struct hda_codec *codec,
				  unsigned char *send_buf,
				  unsigned int send_buf_size,
				  unsigned char *return_buf,
				  unsigned int return_buf_size,
				  unsigned int *bytes_returned)
{
	struct ca0132_spec *spec = codec->spec;
	int retry;
	int status = -1;
	unsigned int scp_send_size = 0;
	unsigned int total_size;
	bool waiting_for_resp = false;
	unsigned int header;
	struct scp_msg *ret_msg;
	unsigned int resp_src_id, resp_target_id;
	unsigned int data_size, src_id, target_id, get_flag, device_flag;

	if (bytes_returned)
		*bytes_returned = 0;

	/* get scp header from buffer */
	header = *((unsigned int *)send_buf);
	extract_scp_header(header, &target_id, &src_id, &get_flag, NULL,
			   &device_flag, NULL, NULL, &data_size);
	scp_send_size = data_size + 1;
	total_size = (scp_send_size * 4);

	if (send_buf_size < total_size)
		return -EINVAL;

	if (get_flag || device_flag) {
		if (!return_buf || return_buf_size < 4 || !bytes_returned)
			return -EINVAL;

		spec->wait_scp_header = *((unsigned int *)send_buf);

		/* swap source id with target id */
		resp_target_id = src_id;
		resp_src_id = target_id;
		spec->wait_scp_header &= 0xffff0000;
		spec->wait_scp_header |= (resp_src_id << 8) | (resp_target_id);
		spec->wait_num_data = return_buf_size/sizeof(unsigned int) - 1;
		spec->wait_scp = 1;
		waiting_for_resp = true;
	}

	status = dspio_write_multiple(codec, (unsigned int *)send_buf,
				      scp_send_size);
	if (status < 0) {
		spec->wait_scp = 0;
		return status;
	}

	if (waiting_for_resp) {
		memset(return_buf, 0, return_buf_size);
		retry = 50;
		do {
			msleep(20);
		} while (spec->wait_scp && (--retry != 0));
		waiting_for_resp = false;
		if (retry != 0) {
			ret_msg = (struct scp_msg *)return_buf;
			memcpy(&ret_msg->hdr, &spec->scp_resp_header, 4);
			memcpy(&ret_msg->data, spec->scp_resp_data,
			       spec->wait_num_data);
			*bytes_returned = (spec->scp_resp_count + 1) * 4;
			status = 0;
		} else {
			status = -EIO;
		}
		spec->wait_scp = 0;
	}

	return status;
}

/**
 * Prepare and send the SCP message to DSP
 * @codec: the HDA codec
 * @mod_id: ID of the DSP module to send the command
 * @req: ID of request to send to the DSP module
 * @dir: SET or GET
 * @data: pointer to the data to send with the request, request specific
 * @len: length of the data, in bytes
 * @reply: point to the buffer to hold data returned for a reply
 * @reply_len: length of the reply buffer returned from GET
 *
 * Returns zero or a negative error code.
 */
static int dspio_scp(struct hda_codec *codec,
		int mod_id, int req, int dir, void *data, unsigned int len,
		void *reply, unsigned int *reply_len)
{
	int status = 0;
	struct scp_msg scp_send, scp_reply;
	unsigned int ret_bytes, send_size, ret_size;
	unsigned int send_get_flag, reply_resp_flag, reply_error_flag;
	unsigned int reply_data_size;

	memset(&scp_send, 0, sizeof(scp_send));
	memset(&scp_reply, 0, sizeof(scp_reply));

	if ((len != 0 && data == NULL) || (len > SCP_MAX_DATA_WORDS))
		return -EINVAL;

	if (dir == SCP_GET && reply == NULL) {
		snd_printdd(KERN_ERR "dspio_scp get but has no buffer");
		return -EINVAL;
	}

	if (reply != NULL && (reply_len == NULL || (*reply_len == 0))) {
		snd_printdd(KERN_ERR "dspio_scp bad resp buf len parms");
		return -EINVAL;
	}

	scp_send.hdr = make_scp_header(mod_id, 0x20, (dir == SCP_GET), req,
				       0, 0, 0, len/sizeof(unsigned int));
	if (data != NULL && len > 0) {
		len = min((unsigned int)(sizeof(scp_send.data)), len);
		memcpy(scp_send.data, data, len);
	}

	ret_bytes = 0;
	send_size = sizeof(unsigned int) + len;
	status = dspio_send_scp_message(codec, (unsigned char *)&scp_send,
					send_size, (unsigned char *)&scp_reply,
					sizeof(scp_reply), &ret_bytes);

	if (status < 0) {
		snd_printdd(KERN_ERR "dspio_scp: send scp msg failed");
		return status;
	}

	/* extract send and reply headers members */
	extract_scp_header(scp_send.hdr, NULL, NULL, &send_get_flag,
			   NULL, NULL, NULL, NULL, NULL);
	extract_scp_header(scp_reply.hdr, NULL, NULL, NULL, NULL, NULL,
			   &reply_resp_flag, &reply_error_flag,
			   &reply_data_size);

	if (!send_get_flag)
		return 0;

	if (reply_resp_flag && !reply_error_flag) {
		ret_size = (ret_bytes - sizeof(scp_reply.hdr))
					/ sizeof(unsigned int);

		if (*reply_len < ret_size*sizeof(unsigned int)) {
			snd_printdd(KERN_ERR "reply too long for buf");
			return -EINVAL;
		} else if (ret_size != reply_data_size) {
			snd_printdd(KERN_ERR "RetLen and HdrLen .NE.");
			return -EINVAL;
		} else {
			*reply_len = ret_size*sizeof(unsigned int);
			memcpy(reply, scp_reply.data, *reply_len);
		}
	} else {
		snd_printdd(KERN_ERR "reply ill-formed or errflag set");
		return -EIO;
	}

	return status;
}

/*
 * Allocate a DSP DMA channel via an SCP message
 */
static int dspio_alloc_dma_chan(struct hda_codec *codec, unsigned int *dma_chan)
{
	int status = 0;
	unsigned int size = sizeof(dma_chan);

	snd_printdd(KERN_INFO "     dspio_alloc_dma_chan() -- begin");
	status = dspio_scp(codec, MASTERCONTROL, MASTERCONTROL_ALLOC_DMA_CHAN,
			SCP_GET, NULL, 0, dma_chan, &size);

	if (status < 0) {
		snd_printdd(KERN_INFO "dspio_alloc_dma_chan: SCP Failed");
		return status;
	}

	if ((*dma_chan + 1) == 0) {
		snd_printdd(KERN_INFO "no free dma channels to allocate");
		return -EBUSY;
	}

	snd_printdd("dspio_alloc_dma_chan: chan=%d\n", *dma_chan);
	snd_printdd(KERN_INFO "     dspio_alloc_dma_chan() -- complete");

	return status;
}

/*
 * Free a DSP DMA via an SCP message
 */
static int dspio_free_dma_chan(struct hda_codec *codec, unsigned int dma_chan)
{
	int status = 0;
	unsigned int dummy = 0;

	snd_printdd(KERN_INFO "     dspio_free_dma_chan() -- begin");
	snd_printdd("dspio_free_dma_chan: chan=%d\n", dma_chan);

	status = dspio_scp(codec, MASTERCONTROL, MASTERCONTROL_ALLOC_DMA_CHAN,
			   SCP_SET, &dma_chan, sizeof(dma_chan), NULL, &dummy);

	if (status < 0) {
		snd_printdd(KERN_INFO "dspio_free_dma_chan: SCP Failed");
		return status;
	}

	snd_printdd(KERN_INFO "     dspio_free_dma_chan() -- complete");

	return status;
}

/*
 * (Re)start the DSP
 */
static int dsp_set_run_state(struct hda_codec *codec)
{
	unsigned int dbg_ctrl_reg;
	unsigned int halt_state;
	int err;

	err = chipio_read(codec, DSP_DBGCNTL_INST_OFFSET, &dbg_ctrl_reg);
	if (err < 0)
		return err;

	halt_state = (dbg_ctrl_reg & DSP_DBGCNTL_STATE_MASK) >>
		      DSP_DBGCNTL_STATE_LOBIT;

	if (halt_state != 0) {
		dbg_ctrl_reg &= ~((halt_state << DSP_DBGCNTL_SS_LOBIT) &
				  DSP_DBGCNTL_SS_MASK);
		err = chipio_write(codec, DSP_DBGCNTL_INST_OFFSET,
				   dbg_ctrl_reg);
		if (err < 0)
			return err;

		dbg_ctrl_reg |= (halt_state << DSP_DBGCNTL_EXEC_LOBIT) &
				DSP_DBGCNTL_EXEC_MASK;
		err = chipio_write(codec, DSP_DBGCNTL_INST_OFFSET,
				   dbg_ctrl_reg);
		if (err < 0)
			return err;
	}

	return 0;
}

/*
 * Reset the DSP
 */
static int dsp_reset(struct hda_codec *codec)
{
	unsigned int res;
	int retry = 20;

	snd_printdd("dsp_reset\n");
	do {
		res = dspio_send(codec, VENDOR_DSPIO_DSP_INIT, 0);
		retry--;
	} while (res == -EIO && retry);

	if (!retry) {
		snd_printdd("dsp_reset timeout\n");
		return -EIO;
	}

	return 0;
}

/*
 * Convert chip address to DSP address
 */
static unsigned int dsp_chip_to_dsp_addx(unsigned int chip_addx,
					bool *code, bool *yram)
{
	*code = *yram = false;

	if (UC_RANGE(chip_addx, 1)) {
		*code = true;
		return UC_OFF(chip_addx);
	} else if (X_RANGE_ALL(chip_addx, 1)) {
		return X_OFF(chip_addx);
	} else if (Y_RANGE_ALL(chip_addx, 1)) {
		*yram = true;
		return Y_OFF(chip_addx);
	}

	return (unsigned int)INVALID_CHIP_ADDRESS;
}

/*
 * Check if the DSP DMA is active
 */
static bool dsp_is_dma_active(struct hda_codec *codec, unsigned int dma_chan)
{
	unsigned int dma_chnlstart_reg;

	chipio_read(codec, DSPDMAC_CHNLSTART_INST_OFFSET, &dma_chnlstart_reg);

	return ((dma_chnlstart_reg & (1 <<
			(DSPDMAC_CHNLSTART_EN_LOBIT + dma_chan))) != 0);
}

static int dsp_dma_setup_common(struct hda_codec *codec,
				unsigned int chip_addx,
				unsigned int dma_chan,
				unsigned int port_map_mask,
				bool ovly)
{
	int status = 0;
	unsigned int chnl_prop;
	unsigned int dsp_addx;
	unsigned int active;
	bool code, yram;

	snd_printdd(KERN_INFO "-- dsp_dma_setup_common() -- Begin ---------");

	if (dma_chan >= DSPDMAC_DMA_CFG_CHANNEL_COUNT) {
		snd_printdd(KERN_ERR "dma chan num invalid");
		return -EINVAL;
	}

	if (dsp_is_dma_active(codec, dma_chan)) {
		snd_printdd(KERN_ERR "dma already active");
		return -EBUSY;
	}

	dsp_addx = dsp_chip_to_dsp_addx(chip_addx, &code, &yram);

	if (dsp_addx == INVALID_CHIP_ADDRESS) {
		snd_printdd(KERN_ERR "invalid chip addr");
		return -ENXIO;
	}

	chnl_prop = DSPDMAC_CHNLPROP_AC_MASK;
	active = 0;

	snd_printdd(KERN_INFO "   dsp_dma_setup_common()    start reg pgm");

	if (ovly) {
		status = chipio_read(codec, DSPDMAC_CHNLPROP_INST_OFFSET,
				     &chnl_prop);

		if (status < 0) {
			snd_printdd(KERN_ERR "read CHNLPROP Reg fail");
			return status;
		}
		snd_printdd(KERN_INFO "dsp_dma_setup_common() Read CHNLPROP");
	}

	if (!code)
		chnl_prop &= ~(1 << (DSPDMAC_CHNLPROP_MSPCE_LOBIT + dma_chan));
	else
		chnl_prop |=  (1 << (DSPDMAC_CHNLPROP_MSPCE_LOBIT + dma_chan));

	chnl_prop &= ~(1 << (DSPDMAC_CHNLPROP_DCON_LOBIT + dma_chan));

	status = chipio_write(codec, DSPDMAC_CHNLPROP_INST_OFFSET, chnl_prop);
	if (status < 0) {
		snd_printdd(KERN_ERR "write CHNLPROP Reg fail");
		return status;
	}
	snd_printdd(KERN_INFO "   dsp_dma_setup_common()    Write CHNLPROP");

	if (ovly) {
		status = chipio_read(codec, DSPDMAC_ACTIVE_INST_OFFSET,
				     &active);

		if (status < 0) {
			snd_printdd(KERN_ERR "read ACTIVE Reg fail");
			return status;
		}
		snd_printdd(KERN_INFO "dsp_dma_setup_common() Read ACTIVE");
	}

	active &= (~(1 << (DSPDMAC_ACTIVE_AAR_LOBIT + dma_chan))) &
		DSPDMAC_ACTIVE_AAR_MASK;

	status = chipio_write(codec, DSPDMAC_ACTIVE_INST_OFFSET, active);
	if (status < 0) {
		snd_printdd(KERN_ERR "write ACTIVE Reg fail");
		return status;
	}

	snd_printdd(KERN_INFO "   dsp_dma_setup_common()    Write ACTIVE");

	status = chipio_write(codec, DSPDMAC_AUDCHSEL_INST_OFFSET(dma_chan),
			      port_map_mask);
	if (status < 0) {
		snd_printdd(KERN_ERR "write AUDCHSEL Reg fail");
		return status;
	}
	snd_printdd(KERN_INFO "   dsp_dma_setup_common()    Write AUDCHSEL");

	status = chipio_write(codec, DSPDMAC_IRQCNT_INST_OFFSET(dma_chan),
			DSPDMAC_IRQCNT_BICNT_MASK | DSPDMAC_IRQCNT_CICNT_MASK);
	if (status < 0) {
		snd_printdd(KERN_ERR "write IRQCNT Reg fail");
		return status;
	}
	snd_printdd(KERN_INFO "   dsp_dma_setup_common()    Write IRQCNT");

	snd_printdd(
		   "ChipA=0x%x,DspA=0x%x,dmaCh=%u, "
		   "CHSEL=0x%x,CHPROP=0x%x,Active=0x%x\n",
		   chip_addx, dsp_addx, dma_chan,
		   port_map_mask, chnl_prop, active);

	snd_printdd(KERN_INFO "-- dsp_dma_setup_common() -- Complete ------");

	return 0;
}

/*
 * Setup the DSP DMA per-transfer-specific registers
 */
static int dsp_dma_setup(struct hda_codec *codec,
			unsigned int chip_addx,
			unsigned int count,
			unsigned int dma_chan)
{
	int status = 0;
	bool code, yram;
	unsigned int dsp_addx;
	unsigned int addr_field;
	unsigned int incr_field;
	unsigned int base_cnt;
	unsigned int cur_cnt;
	unsigned int dma_cfg = 0;
	unsigned int adr_ofs = 0;
	unsigned int xfr_cnt = 0;
	const unsigned int max_dma_count = 1 << (DSPDMAC_XFRCNT_BCNT_HIBIT -
						DSPDMAC_XFRCNT_BCNT_LOBIT + 1);

	snd_printdd(KERN_INFO "-- dsp_dma_setup() -- Begin ---------");

	if (count > max_dma_count) {
		snd_printdd(KERN_ERR "count too big");
		return -EINVAL;
	}

	dsp_addx = dsp_chip_to_dsp_addx(chip_addx, &code, &yram);
	if (dsp_addx == INVALID_CHIP_ADDRESS) {
		snd_printdd(KERN_ERR "invalid chip addr");
		return -ENXIO;
	}

	snd_printdd(KERN_INFO "   dsp_dma_setup()    start reg pgm");

	addr_field = dsp_addx << DSPDMAC_DMACFG_DBADR_LOBIT;
	incr_field   = 0;

	if (!code) {
		addr_field <<= 1;
		if (yram)
			addr_field |= (1 << DSPDMAC_DMACFG_DBADR_LOBIT);

		incr_field  = (1 << DSPDMAC_DMACFG_AINCR_LOBIT);
	}

	dma_cfg = addr_field + incr_field;
	status = chipio_write(codec, DSPDMAC_DMACFG_INST_OFFSET(dma_chan),
				dma_cfg);
	if (status < 0) {
		snd_printdd(KERN_ERR "write DMACFG Reg fail");
		return status;
	}
	snd_printdd(KERN_INFO "   dsp_dma_setup()    Write DMACFG");

	adr_ofs = (count - 1) << (DSPDMAC_DSPADROFS_BOFS_LOBIT +
							(code ? 0 : 1));

	status = chipio_write(codec, DSPDMAC_DSPADROFS_INST_OFFSET(dma_chan),
				adr_ofs);
	if (status < 0) {
		snd_printdd(KERN_ERR "write DSPADROFS Reg fail");
		return status;
	}
	snd_printdd(KERN_INFO "   dsp_dma_setup()    Write DSPADROFS");

	base_cnt = (count - 1) << DSPDMAC_XFRCNT_BCNT_LOBIT;

	cur_cnt  = (count - 1) << DSPDMAC_XFRCNT_CCNT_LOBIT;

	xfr_cnt = base_cnt | cur_cnt;

	status = chipio_write(codec,
				DSPDMAC_XFRCNT_INST_OFFSET(dma_chan), xfr_cnt);
	if (status < 0) {
		snd_printdd(KERN_ERR "write XFRCNT Reg fail");
		return status;
	}
	snd_printdd(KERN_INFO "   dsp_dma_setup()    Write XFRCNT");

	snd_printdd(
		   "ChipA=0x%x, cnt=0x%x, DMACFG=0x%x, "
		   "ADROFS=0x%x, XFRCNT=0x%x\n",
		   chip_addx, count, dma_cfg, adr_ofs, xfr_cnt);

	snd_printdd(KERN_INFO "-- dsp_dma_setup() -- Complete ---------");

	return 0;
}

/*
 * Start the DSP DMA
 */
static int dsp_dma_start(struct hda_codec *codec,
			 unsigned int dma_chan, bool ovly)
{
	unsigned int reg = 0;
	int status = 0;

	snd_printdd(KERN_INFO "-- dsp_dma_start() -- Begin ---------");

	if (ovly) {
		status = chipio_read(codec,
				     DSPDMAC_CHNLSTART_INST_OFFSET, &reg);

		if (status < 0) {
			snd_printdd(KERN_ERR "read CHNLSTART reg fail");
			return status;
		}
		snd_printdd(KERN_INFO "-- dsp_dma_start()    Read CHNLSTART");

		reg &= ~(DSPDMAC_CHNLSTART_EN_MASK |
				DSPDMAC_CHNLSTART_DIS_MASK);
	}

	status = chipio_write(codec, DSPDMAC_CHNLSTART_INST_OFFSET,
			reg | (1 << (dma_chan + DSPDMAC_CHNLSTART_EN_LOBIT)));
	if (status < 0) {
		snd_printdd(KERN_ERR "write CHNLSTART reg fail");
		return status;
	}
	snd_printdd(KERN_INFO "-- dsp_dma_start() -- Complete ---------");

	return status;
}

/*
 * Stop the DSP DMA
 */
static int dsp_dma_stop(struct hda_codec *codec,
			unsigned int dma_chan, bool ovly)
{
	unsigned int reg = 0;
	int status = 0;

	snd_printdd(KERN_INFO "-- dsp_dma_stop() -- Begin ---------");

	if (ovly) {
		status = chipio_read(codec,
				     DSPDMAC_CHNLSTART_INST_OFFSET, &reg);

		if (status < 0) {
			snd_printdd(KERN_ERR "read CHNLSTART reg fail");
			return status;
		}
		snd_printdd(KERN_INFO "-- dsp_dma_stop()    Read CHNLSTART");
		reg &= ~(DSPDMAC_CHNLSTART_EN_MASK |
				DSPDMAC_CHNLSTART_DIS_MASK);
	}

	status = chipio_write(codec, DSPDMAC_CHNLSTART_INST_OFFSET,
			reg | (1 << (dma_chan + DSPDMAC_CHNLSTART_DIS_LOBIT)));
	if (status < 0) {
		snd_printdd(KERN_ERR "write CHNLSTART reg fail");
		return status;
	}
	snd_printdd(KERN_INFO "-- dsp_dma_stop() -- Complete ---------");

	return status;
}

/**
 * Allocate router ports
 *
 * @codec: the HDA codec
 * @num_chans: number of channels in the stream
 * @ports_per_channel: number of ports per channel
 * @start_device: start device
 * @port_map: pointer to the port list to hold the allocated ports
 *
 * Returns zero or a negative error code.
 */
static int dsp_allocate_router_ports(struct hda_codec *codec,
				     unsigned int num_chans,
				     unsigned int ports_per_channel,
				     unsigned int start_device,
				     unsigned int *port_map)
{
	int status = 0;
	int res;
	u8 val;

	status = chipio_send(codec, VENDOR_CHIPIO_STATUS, 0);
	if (status < 0)
		return status;

	val = start_device << 6;
	val |= (ports_per_channel - 1) << 4;
	val |= num_chans - 1;

	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PORT_ALLOC_CONFIG_SET,
			    val);

	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PORT_ALLOC_SET,
			    MEM_CONNID_DSP);

	status = chipio_send(codec, VENDOR_CHIPIO_STATUS, 0);
	if (status < 0)
		return status;

	res = snd_hda_codec_read(codec, WIDGET_CHIP_CTRL, 0,
				VENDOR_CHIPIO_PORT_ALLOC_GET, 0);

	*port_map = res;

	return (res < 0) ? res : 0;
}

/*
 * Free router ports
 */
static int dsp_free_router_ports(struct hda_codec *codec)
{
	int status = 0;

	status = chipio_send(codec, VENDOR_CHIPIO_STATUS, 0);
	if (status < 0)
		return status;

	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PORT_FREE_SET,
			    MEM_CONNID_DSP);

	status = chipio_send(codec, VENDOR_CHIPIO_STATUS, 0);

	return status;
}

/*
 * Allocate DSP ports for the download stream
 */
static int dsp_allocate_ports(struct hda_codec *codec,
			unsigned int num_chans,
			unsigned int rate_multi, unsigned int *port_map)
{
	int status;

	snd_printdd(KERN_INFO "     dsp_allocate_ports() -- begin");

	if ((rate_multi != 1) && (rate_multi != 2) && (rate_multi != 4)) {
		snd_printdd(KERN_ERR "bad rate multiple");
		return -EINVAL;
	}

	status = dsp_allocate_router_ports(codec, num_chans,
					   rate_multi, 0, port_map);

	snd_printdd(KERN_INFO "     dsp_allocate_ports() -- complete");

	return status;
}

static int dsp_allocate_ports_format(struct hda_codec *codec,
			const unsigned short fmt,
			unsigned int *port_map)
{
	int status;
	unsigned int num_chans;

	unsigned int sample_rate_div = ((get_hdafmt_rate(fmt) >> 0) & 3) + 1;
	unsigned int sample_rate_mul = ((get_hdafmt_rate(fmt) >> 3) & 3) + 1;
	unsigned int rate_multi = sample_rate_mul / sample_rate_div;

	if ((rate_multi != 1) && (rate_multi != 2) && (rate_multi != 4)) {
		snd_printdd(KERN_ERR "bad rate multiple");
		return -EINVAL;
	}

	num_chans = get_hdafmt_chs(fmt) + 1;

	status = dsp_allocate_ports(codec, num_chans, rate_multi, port_map);

	return status;
}

/*
 * free DSP ports
 */
static int dsp_free_ports(struct hda_codec *codec)
{
	int status;

	snd_printdd(KERN_INFO "     dsp_free_ports() -- begin");

	status = dsp_free_router_ports(codec);
	if (status < 0) {
		snd_printdd(KERN_ERR "free router ports fail");
		return status;
	}
	snd_printdd(KERN_INFO "     dsp_free_ports() -- complete");

	return status;
}

/*
 *  HDA DMA engine stuffs for DSP code download
 */
struct dma_engine {
	struct hda_codec *codec;
	unsigned short m_converter_format;
	struct snd_dma_buffer *dmab;
	unsigned int buf_size;
};


enum dma_state {
	DMA_STATE_STOP  = 0,
	DMA_STATE_RUN   = 1
};

static int dma_convert_to_hda_format(
		unsigned int sample_rate,
		unsigned short channels,
		unsigned short *hda_format)
{
	unsigned int format_val;

	format_val = snd_hda_calc_stream_format(
				sample_rate,
				channels,
				SNDRV_PCM_FORMAT_S32_LE,
				32, 0);

	if (hda_format)
		*hda_format = (unsigned short)format_val;

	return 0;
}

/*
 *  Reset DMA for DSP download
 */
static int dma_reset(struct dma_engine *dma)
{
	struct hda_codec *codec = dma->codec;
	struct ca0132_spec *spec = codec->spec;
	int status;

	if (dma->dmab)
		snd_hda_codec_load_dsp_cleanup(codec, dma->dmab);

	status = snd_hda_codec_load_dsp_prepare(codec,
			dma->m_converter_format,
			dma->buf_size,
			dma->dmab);
	if (status < 0)
		return status;
	spec->dsp_stream_id = status;
	return 0;
}

static int dma_set_state(struct dma_engine *dma, enum dma_state state)
{
	bool cmd;

	snd_printdd("dma_set_state state=%d\n", state);

	switch (state) {
	case DMA_STATE_STOP:
		cmd = false;
		break;
	case DMA_STATE_RUN:
		cmd = true;
		break;
	default:
		return 0;
	}

	snd_hda_codec_load_dsp_trigger(dma->codec, cmd);
	return 0;
}

static unsigned int dma_get_buffer_size(struct dma_engine *dma)
{
	return dma->dmab->bytes;
}

static unsigned char *dma_get_buffer_addr(struct dma_engine *dma)
{
	return dma->dmab->area;
}

static int dma_xfer(struct dma_engine *dma,
		const unsigned int *data,
		unsigned int count)
{
	memcpy(dma->dmab->area, data, count);
	return 0;
}

static void dma_get_converter_format(
		struct dma_engine *dma,
		unsigned short *format)
{
	if (format)
		*format = dma->m_converter_format;
}

static unsigned int dma_get_stream_id(struct dma_engine *dma)
{
	struct ca0132_spec *spec = dma->codec->spec;

	return spec->dsp_stream_id;
}

struct dsp_image_seg {
	u32 magic;
	u32 chip_addr;
	u32 count;
	u32 data[0];
};

static const u32 g_magic_value = 0x4c46584d;
static const u32 g_chip_addr_magic_value = 0xFFFFFF01;

static bool is_valid(const struct dsp_image_seg *p)
{
	return p->magic == g_magic_value;
}

static bool is_hci_prog_list_seg(const struct dsp_image_seg *p)
{
	return g_chip_addr_magic_value == p->chip_addr;
}

static bool is_last(const struct dsp_image_seg *p)
{
	return p->count == 0;
}

static size_t dsp_sizeof(const struct dsp_image_seg *p)
{
	return sizeof(*p) + p->count*sizeof(u32);
}

static const struct dsp_image_seg *get_next_seg_ptr(
				const struct dsp_image_seg *p)
{
	return (struct dsp_image_seg *)((unsigned char *)(p) + dsp_sizeof(p));
}

/*
 * CA0132 chip DSP transfer stuffs.  For DSP download.
 */
#define INVALID_DMA_CHANNEL (~0UL)

/*
 * Program a list of address/data pairs via the ChipIO widget.
 * The segment data is in the format of successive pairs of words.
 * These are repeated as indicated by the segment's count field.
 */
static int dspxfr_hci_write(struct hda_codec *codec,
			const struct dsp_image_seg *fls)
{
	int status;
	const u32 *data;
	unsigned int count;

	if (fls == NULL || fls->chip_addr != g_chip_addr_magic_value) {
		snd_printdd(KERN_ERR "hci_write invalid params");
		return -EINVAL;
	}

	count = fls->count;
	data = (u32 *)(fls->data);
	while (count >= 2) {
		status = chipio_write(codec, data[0], data[1]);
		if (status < 0) {
			snd_printdd(KERN_ERR "hci_write chipio failed");
			return status;
		}
		count -= 2;
		data  += 2;
	}
	return 0;
}

/**
 * Write a block of data into DSP code or data RAM using pre-allocated
 * DMA engine.
 *
 * @codec: the HDA codec
 * @fls: pointer to a fast load image
 * @reloc: Relocation address for loading single-segment overlays, or 0 for
 *	   no relocation
 * @dma_engine: pointer to DMA engine to be used for DSP download
 * @dma_chan: The number of DMA channels used for DSP download
 * @port_map_mask: port mapping
 * @ovly: TRUE if overlay format is required
 *
 * Returns zero or a negative error code.
 */
static int dspxfr_one_seg(struct hda_codec *codec,
			const struct dsp_image_seg *fls,
			unsigned int reloc,
			struct dma_engine *dma_engine,
			unsigned int dma_chan,
			unsigned int port_map_mask,
			bool ovly)
{
	int status;
	bool comm_dma_setup_done = false;
	const unsigned int *data;
	unsigned int chip_addx;
	unsigned int words_to_write;
	unsigned int buffer_size_words;
	unsigned char *buffer_addx;
	unsigned short hda_format;
	unsigned int sample_rate_div;
	unsigned int sample_rate_mul;
	unsigned int num_chans;
	unsigned int hda_frame_size_words;
	unsigned int remainder_words;
	const u32 *data_remainder;
	u32 chip_addx_remainder;
	unsigned int run_size_words;
	const struct dsp_image_seg *hci_write = NULL;
	int retry;

	if (fls == NULL)
		return -EINVAL;
	if (is_hci_prog_list_seg(fls)) {
		hci_write = fls;
		fls = get_next_seg_ptr(fls);
	}

	if (hci_write && (!fls || is_last(fls))) {
		snd_printdd("hci_write\n");
		return dspxfr_hci_write(codec, hci_write);
	}

	if (fls == NULL || dma_engine == NULL || port_map_mask == 0) {
		snd_printdd("Invalid Params\n");
		return -EINVAL;
	}

	data = fls->data;
	chip_addx = fls->chip_addr,
	words_to_write = fls->count;

	if (!words_to_write)
		return hci_write ? dspxfr_hci_write(codec, hci_write) : 0;
	if (reloc)
		chip_addx = (chip_addx & (0xFFFF0000 << 2)) + (reloc << 2);

	if (!UC_RANGE(chip_addx, words_to_write) &&
	    !X_RANGE_ALL(chip_addx, words_to_write) &&
	    !Y_RANGE_ALL(chip_addx, words_to_write)) {
		snd_printdd("Invalid chip_addx Params\n");
		return -EINVAL;
	}

	buffer_size_words = (unsigned int)dma_get_buffer_size(dma_engine) /
					sizeof(u32);

	buffer_addx = dma_get_buffer_addr(dma_engine);

	if (buffer_addx == NULL) {
		snd_printdd(KERN_ERR "dma_engine buffer NULL\n");
		return -EINVAL;
	}

	dma_get_converter_format(dma_engine, &hda_format);
	sample_rate_div = ((get_hdafmt_rate(hda_format) >> 0) & 3) + 1;
	sample_rate_mul = ((get_hdafmt_rate(hda_format) >> 3) & 3) + 1;
	num_chans = get_hdafmt_chs(hda_format) + 1;

	hda_frame_size_words = ((sample_rate_div == 0) ? 0 :
			(num_chans * sample_rate_mul / sample_rate_div));

	buffer_size_words = min(buffer_size_words,
				(unsigned int)(UC_RANGE(chip_addx, 1) ?
				65536 : 32768));
	buffer_size_words -= buffer_size_words % hda_frame_size_words;
	snd_printdd(
		   "chpadr=0x%08x frmsz=%u nchan=%u "
		   "rate_mul=%u div=%u bufsz=%u\n",
		   chip_addx, hda_frame_size_words, num_chans,
		   sample_rate_mul, sample_rate_div, buffer_size_words);

	if ((buffer_addx == NULL) || (hda_frame_size_words == 0) ||
	    (buffer_size_words < hda_frame_size_words)) {
		snd_printdd(KERN_ERR "dspxfr_one_seg:failed\n");
		return -EINVAL;
	}

	remainder_words = words_to_write % hda_frame_size_words;
	data_remainder = data;
	chip_addx_remainder = chip_addx;

	data += remainder_words;
	chip_addx += remainder_words*sizeof(u32);
	words_to_write -= remainder_words;

	while (words_to_write != 0) {
		run_size_words = min(buffer_size_words, words_to_write);
		snd_printdd("dspxfr (seg loop)cnt=%u rs=%u remainder=%u\n",
			    words_to_write, run_size_words, remainder_words);
		dma_xfer(dma_engine, data, run_size_words*sizeof(u32));
		if (!comm_dma_setup_done) {
			status = dsp_dma_stop(codec, dma_chan, ovly);
			if (status < 0)
				return -EIO;
			status = dsp_dma_setup_common(codec, chip_addx,
						dma_chan, port_map_mask, ovly);
			if (status < 0)
				return status;
			comm_dma_setup_done = true;
		}

		status = dsp_dma_setup(codec, chip_addx,
						run_size_words, dma_chan);
		if (status < 0)
			return status;
		status = dsp_dma_start(codec, dma_chan, ovly);
		if (status < 0)
			return status;
		if (!dsp_is_dma_active(codec, dma_chan)) {
			snd_printdd(KERN_ERR "dspxfr:DMA did not start");
			return -EIO;
		}
		status = dma_set_state(dma_engine, DMA_STATE_RUN);
		if (status < 0)
			return status;
		if (remainder_words != 0) {
			status = chipio_write_multiple(codec,
						chip_addx_remainder,
						data_remainder,
						remainder_words);
			remainder_words = 0;
		}
		if (hci_write) {
			status = dspxfr_hci_write(codec, hci_write);
			hci_write = NULL;
		}
		retry = 5000;
		while (dsp_is_dma_active(codec, dma_chan)) {
			if (--retry <= 0)
				break;
		}
		snd_printdd(KERN_INFO "+++++ DMA complete");
		dma_set_state(dma_engine, DMA_STATE_STOP);
		dma_reset(dma_engine);

		if (status < 0)
			return status;

		data += run_size_words;
		chip_addx += run_size_words*sizeof(u32);
		words_to_write -= run_size_words;
	}

	if (remainder_words != 0) {
		status = chipio_write_multiple(codec, chip_addx_remainder,
					data_remainder, remainder_words);
	}

	return status;
}

/**
 * Write the entire DSP image of a DSP code/data overlay to DSP memories
 *
 * @codec: the HDA codec
 * @fls_data: pointer to a fast load image
 * @reloc: Relocation address for loading single-segment overlays, or 0 for
 *	   no relocation
 * @sample_rate: sampling rate of the stream used for DSP download
 * @number_channels: channels of the stream used for DSP download
 * @ovly: TRUE if overlay format is required
 *
 * Returns zero or a negative error code.
 */
static int dspxfr_image(struct hda_codec *codec,
			const struct dsp_image_seg *fls_data,
			unsigned int reloc,
			unsigned int sample_rate,
			unsigned short channels,
			bool ovly)
{
	struct ca0132_spec *spec = codec->spec;
	int status;
	unsigned short hda_format = 0;
	unsigned int response;
	unsigned char stream_id = 0;
	struct dma_engine *dma_engine;
	unsigned int dma_chan;
	unsigned int port_map_mask;

	if (fls_data == NULL)
		return -EINVAL;

	dma_engine = kzalloc(sizeof(*dma_engine), GFP_KERNEL);
	if (!dma_engine) {
		status = -ENOMEM;
		goto exit;
	}
	memset((void *)dma_engine, 0, sizeof(*dma_engine));

	dma_engine->dmab = kzalloc(sizeof(*dma_engine->dmab), GFP_KERNEL);
	if (!dma_engine->dmab) {
		status = -ENOMEM;
		goto exit;
	}

	dma_engine->codec = codec;
	dma_convert_to_hda_format(sample_rate, channels, &hda_format);
	dma_engine->m_converter_format = hda_format;
	dma_engine->buf_size = (ovly ? DSP_DMA_WRITE_BUFLEN_OVLY :
			DSP_DMA_WRITE_BUFLEN_INIT) * 2;

	dma_chan = 0;

	status = codec_set_converter_format(codec, WIDGET_CHIP_CTRL,
					hda_format, &response);

	if (status < 0) {
		snd_printdd(KERN_ERR "set converter format fail");
		goto exit;
	}

	status = snd_hda_codec_load_dsp_prepare(codec,
				dma_engine->m_converter_format,
				dma_engine->buf_size,
				dma_engine->dmab);
	if (status < 0)
		goto exit;
	spec->dsp_stream_id = status;

	if (ovly) {
		status = dspio_alloc_dma_chan(codec, &dma_chan);
		if (status < 0) {
			snd_printdd(KERN_ERR "alloc dmachan fail");
			dma_chan = (unsigned int)INVALID_DMA_CHANNEL;
			goto exit;
		}
	}

	port_map_mask = 0;
	status = dsp_allocate_ports_format(codec, hda_format,
					&port_map_mask);
	if (status < 0) {
		snd_printdd(KERN_ERR "alloc ports fail");
		goto exit;
	}

	stream_id = dma_get_stream_id(dma_engine);
	status = codec_set_converter_stream_channel(codec,
			WIDGET_CHIP_CTRL, stream_id, 0, &response);
	if (status < 0) {
		snd_printdd(KERN_ERR "set stream chan fail");
		goto exit;
	}

	while ((fls_data != NULL) && !is_last(fls_data)) {
		if (!is_valid(fls_data)) {
			snd_printdd(KERN_ERR "FLS check fail");
			status = -EINVAL;
			goto exit;
		}
		status = dspxfr_one_seg(codec, fls_data, reloc,
					dma_engine, dma_chan,
					port_map_mask, ovly);
		if (status < 0)
			break;

		if (is_hci_prog_list_seg(fls_data))
			fls_data = get_next_seg_ptr(fls_data);

		if ((fls_data != NULL) && !is_last(fls_data))
			fls_data = get_next_seg_ptr(fls_data);
	}

	if (port_map_mask != 0)
		status = dsp_free_ports(codec);

	if (status < 0)
		goto exit;

	status = codec_set_converter_stream_channel(codec,
				WIDGET_CHIP_CTRL, 0, 0, &response);

exit:
	if (ovly && (dma_chan != INVALID_DMA_CHANNEL))
		dspio_free_dma_chan(codec, dma_chan);

	if (dma_engine->dmab)
		snd_hda_codec_load_dsp_cleanup(codec, dma_engine->dmab);
	kfree(dma_engine->dmab);
	kfree(dma_engine);

	return status;
}

/*
 * CA0132 DSP download stuffs.
 */
static void dspload_post_setup(struct hda_codec *codec)
{
	snd_printdd(KERN_INFO "---- dspload_post_setup ------");

	/*set DSP speaker to 2.0 configuration*/
	chipio_write(codec, XRAM_XRAM_INST_OFFSET(0x18), 0x08080080);
	chipio_write(codec, XRAM_XRAM_INST_OFFSET(0x19), 0x3f800000);

	/*update write pointer*/
	chipio_write(codec, XRAM_XRAM_INST_OFFSET(0x29), 0x00000002);
}

/**
 * Download DSP from a DSP Image Fast Load structure. This structure is a
 * linear, non-constant sized element array of structures, each of which
 * contain the count of the data to be loaded, the data itself, and the
 * corresponding starting chip address of the starting data location.
 *
 * @codec: the HDA codec
 * @fls: pointer to a fast load image
 * @ovly: TRUE if overlay format is required
 * @reloc: Relocation address for loading single-segment overlays, or 0 for
 *	   no relocation
 * @autostart: TRUE if DSP starts after loading; ignored if ovly is TRUE
 * @router_chans: number of audio router channels to be allocated (0 means use
 *		  internal defaults; max is 32)
 *
 * Returns zero or a negative error code.
 */
static int dspload_image(struct hda_codec *codec,
			const struct dsp_image_seg *fls,
			bool ovly,
			unsigned int reloc,
			bool autostart,
			int router_chans)
{
	int status = 0;
	unsigned int sample_rate;
	unsigned short channels;

	snd_printdd(KERN_INFO "---- dspload_image begin ------");
	if (router_chans == 0) {
		if (!ovly)
			router_chans = DMA_TRANSFER_FRAME_SIZE_NWORDS;
		else
			router_chans = DMA_OVERLAY_FRAME_SIZE_NWORDS;
	}

	sample_rate = 48000;
	channels = (unsigned short)router_chans;

	while (channels > 16) {
		sample_rate *= 2;
		channels /= 2;
	}

	do {
		snd_printdd(KERN_INFO "Ready to program DMA");
		if (!ovly)
			status = dsp_reset(codec);

		if (status < 0)
			break;

		snd_printdd(KERN_INFO "dsp_reset() complete");
		status = dspxfr_image(codec, fls, reloc, sample_rate, channels,
				      ovly);

		if (status < 0)
			break;

		snd_printdd(KERN_INFO "dspxfr_image() complete");
		if (autostart && !ovly) {
			dspload_post_setup(codec);
			status = dsp_set_run_state(codec);
		}

		snd_printdd(KERN_INFO "LOAD FINISHED");
	} while (0);

	return status;
}

static const struct firmware *fw_efx;

static int request_firmware_cached(const struct firmware **firmware_p,
	const char *name, struct device *device)
{
	if (*firmware_p)
		return 0;  /* already loaded */
	return request_firmware(firmware_p, name, device);
}

static void release_cached_firmware(void)
{
	if (fw_efx) {
		release_firmware(fw_efx);
		fw_efx = NULL;
	}
}

static bool dspload_is_loaded(struct hda_codec *codec)
{
	unsigned int data = 0;
	int status = 0;

	status = chipio_read(codec, 0x40004, &data);
	if ((status < 0) || (data != 1))
		return false;

	return true;
}

static bool dspload_wait_loaded(struct hda_codec *codec)
{
	int retry = 100;

	do {
		msleep(20);
		if (dspload_is_loaded(codec)) {
			pr_info("ca0132 DOWNLOAD OK :-) DSP IS RUNNING.\n");
			return true;
		}
	} while (--retry);

	pr_err("ca0132 DOWNLOAD FAILED!!! DSP IS NOT RUNNING.\n");
	return false;
}

/*
 * PCM callbacks
 */
static int ca0132_playback_pcm_open(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream,
					     hinfo);
}

static int ca0132_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
			struct hda_codec *codec,
			unsigned int stream_tag,
			unsigned int format,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_prepare(codec, &spec->multiout,
						stream_tag, format, substream);
}

static int ca0132_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
			struct hda_codec *codec,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

/*
 * Digital out
 */
static int ca0132_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int ca0132_dig_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
			struct hda_codec *codec,
			unsigned int stream_tag,
			unsigned int format,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout,
					     stream_tag, format, substream);
}

static int ca0132_dig_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
			struct hda_codec *codec,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_cleanup(codec, &spec->multiout);
}

static int ca0132_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

/*
 */
static struct hda_pcm_stream ca0132_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.ops = {
		.open = ca0132_playback_pcm_open,
		.prepare = ca0132_playback_pcm_prepare,
		.cleanup = ca0132_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream ca0132_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
};

static struct hda_pcm_stream ca0132_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.ops = {
		.open = ca0132_dig_playback_pcm_open,
		.close = ca0132_dig_playback_pcm_close,
		.prepare = ca0132_dig_playback_pcm_prepare,
		.cleanup = ca0132_dig_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream ca0132_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
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
		goto exit;

	/* *valp 0 is mute, 1 is unmute */
	data = (data & 0x7f) | (*valp ? 0 : 0x80);
	err = chipio_write(codec, REG_CODEC_MUTE, data);
	if (err < 0)
		goto exit;

	spec->curr_hp_switch = *valp;

 exit:
	snd_hda_power_down(codec);
	return err < 0 ? err : 1;
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
		goto exit;

	/* *valp 0 is mute, 1 is unmute */
	data = (data & 0xef) | (*valp ? 0 : 0x10);
	err = chipio_write(codec, REG_CODEC_MUTE, data);
	if (err < 0)
		goto exit;

	spec->curr_speaker_switch = *valp;

 exit:
	snd_hda_power_down(codec);
	return err < 0 ? err : 1;
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
		goto exit;

	val = 31 - left_vol;
	data = (data & 0xe0) | val;
	err = chipio_write(codec, REG_CODEC_HP_VOL_L, data);
	if (err < 0)
		goto exit;

	val = 31 - right_vol;
	data = (data & 0xe0) | val;
	err = chipio_write(codec, REG_CODEC_HP_VOL_R, data);
	if (err < 0)
		goto exit;

	spec->curr_hp_volume[0] = left_vol;
	spec->curr_hp_volume[1] = right_vol;

 exit:
	snd_hda_power_down(codec);
	return err < 0 ? err : 1;
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
		err = snd_hda_create_spdif_share_sw(codec, &spec->multiout);
		if (err < 0)
			return err;
		/* spec->multiout.share_spdif = 1; */
	}

	if (spec->dig_in) {
		err = snd_hda_create_spdif_in_ctls(codec, spec->dig_in);
		if (err < 0)
			return err;
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

	codec->pcm_format_first = 1;
	codec->no_sticky_stream = 1;

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
	spec->input_labels[0] = "Mic";
	spec->adcs[0] = 0x07;

	/* Line-In */
	spec->input_pins[1] = 0x11;
	spec->input_labels[1] = "Line";
	spec->adcs[1] = 0x08;
	spec->num_inputs = 2;

	/* SPDIF I/O */
	spec->dig_out = 0x05;
	spec->multiout.dig_out_nid = spec->dig_out;
	cfg->dig_out_pins[0] = 0x0c;
	cfg->dig_outs = 1;
	cfg->dig_out_type[0] = HDA_PCM_TYPE_SPDIF;
	spec->dig_in = 0x09;
	cfg->dig_in_pin = 0x0e;
	cfg->dig_in_type = HDA_PCM_TYPE_SPDIF;
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

static void ca0132_set_dsp_msr(struct hda_codec *codec, bool is96k)
{
	chipio_set_control_flag(codec, CONTROL_FLAG_DSP_96KHZ, is96k);
	chipio_set_control_flag(codec, CONTROL_FLAG_DAC_96KHZ, is96k);
	chipio_set_control_flag(codec, CONTROL_FLAG_SRC_RATE_96KHZ, is96k);
	chipio_set_control_flag(codec, CONTROL_FLAG_SRC_CLOCK_196MHZ, is96k);
	chipio_set_control_flag(codec, CONTROL_FLAG_ADC_B_96KHZ, is96k);
	chipio_set_control_flag(codec, CONTROL_FLAG_ADC_C_96KHZ, is96k);

	chipio_set_conn_rate(codec, MEM_CONNID_MICIN1, SR_16_000);
	chipio_set_conn_rate(codec, MEM_CONNID_MICOUT1, SR_16_000);
	chipio_set_conn_rate(codec, MEM_CONNID_WUH, SR_48_000);
}

static bool ca0132_download_dsp_images(struct hda_codec *codec)
{
	bool dsp_loaded = false;
	const struct dsp_image_seg *dsp_os_image;

	if (request_firmware_cached(&fw_efx, EFX_FILE,
				    codec->bus->card->dev) != 0)
		return false;

	dsp_os_image = (struct dsp_image_seg *)(fw_efx->data);
	dspload_image(codec, dsp_os_image, 0, 0, true, 0);
	dsp_loaded = dspload_wait_loaded(codec);

	return dsp_loaded;
}

static void ca0132_download_dsp(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	spec->dsp_state = DSP_DOWNLOAD_INIT;

	if (spec->dsp_state == DSP_DOWNLOAD_INIT) {
		chipio_enable_clocks(codec);
		spec->dsp_state = DSP_DOWNLOADING;
		if (!ca0132_download_dsp_images(codec))
			spec->dsp_state = DSP_DOWNLOAD_FAILED;
		else
			spec->dsp_state = DSP_DOWNLOADED;
	}

	if (spec->dsp_state == DSP_DOWNLOADED)
		ca0132_set_dsp_msr(codec, true);
}

static int ca0132_init(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

#ifdef CONFIG_SND_HDA_DSP_LOADER
	ca0132_download_dsp(codec);
#endif

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
	release_cached_firmware();
	snd_hda_delete_codec_preset(&ca0132_list);
}

module_init(patch_ca0132_init)
module_exit(patch_ca0132_exit)
