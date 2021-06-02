// SPDX-License-Identifier: GPL-2.0
/*
 *   Focusrite Scarlett 6i6/18i8/18i20 Gen 2 Driver for ALSA
 *
 *   Copyright (c) 2018-2019 by Geoffrey D. Bennett <g at b4.vu>
 *
 *   Based on the Scarlett (Gen 1) Driver for ALSA:
 *
 *   Copyright (c) 2013 by Tobias Hoffmann
 *   Copyright (c) 2013 by Robin Gareus <robin at gareus.org>
 *   Copyright (c) 2002 by Takashi Iwai <tiwai at suse.de>
 *   Copyright (c) 2014 by Chris J Arges <chris.j.arges at canonical.com>
 *
 *   Many codes borrowed from audio.c by
 *     Alan Cox (alan at lxorguk.ukuu.org.uk)
 *     Thomas Sailer (sailer at ife.ee.ethz.ch)
 *
 *   Code cleanup:
 *   David Henningsson <david.henningsson at canonical.com>
 */

/* Mixer Interface for the Focusrite Scarlett 6i6/18i8/18i20 Gen 2 audio
 * interface. Based on the Gen 1 driver and rewritten.
 */

/* The protocol was reverse engineered by looking at the communication
 * between Focusrite Control 2.3.4 and the Focusrite(R) Scarlett 18i20
 * (firmware 1083) using usbmon in July-August 2018.
 *
 * Scarlett 18i8 support added in April 2019.
 *
 * Scarlett 6i6 support added in June 2019 (thanks to Martin Wittmann
 * for providing usbmon output and testing).
 *
 * This ALSA mixer gives access to:
 *  - input, output, mixer-matrix muxes
 *  - 18x10 mixer-matrix gain stages
 *  - gain/volume controls
 *  - level meters
 *  - line/inst level and pad controls
 *
 * <ditaa>
 *    /--------------\    18chn            20chn     /--------------\
 *    | Hardware  in +--+------\    /-------------+--+ ALSA PCM out |
 *    \--------------/  |      |    |             |  \--------------/
 *                      |      |    |    /-----\  |
 *                      |      |    |    |     |  |
 *                      |      v    v    v     |  |
 *                      |   +---------------+  |  |
 *                      |    \ Matrix  Mux /   |  |
 *                      |     +-----+-----+    |  |
 *                      |           |          |  |
 *                      |           |18chn     |  |
 *                      |           |          |  |
 *                      |           |     10chn|  |
 *                      |           v          |  |
 *                      |     +------------+   |  |
 *                      |     | Mixer      |   |  |
 *                      |     |     Matrix |   |  |
 *                      |     |            |   |  |
 *                      |     | 18x10 Gain |   |  |
 *                      |     |   stages   |   |  |
 *                      |     +-----+------+   |  |
 *                      |           |          |  |
 *                      |18chn      |10chn     |  |20chn
 *                      |           |          |  |
 *                      |           +----------/  |
 *                      |           |             |
 *                      v           v             v
 *                      ===========================
 *               +---------------+       +--—------------+
 *                \ Output  Mux /         \ Capture Mux /
 *                 +---+---+---+           +-----+-----+
 *                     |   |                     |
 *                10chn|   |                     |18chn
 *                     |   |                     |
 *  /--------------\   |   |                     |   /--------------\
 *  | S/PDIF, ADAT |<--/   |10chn                \-->| ALSA PCM in  |
 *  | Hardware out |       |                         \--------------/
 *  \--------------/       |
 *                         v
 *                  +-------------+    Software gain per channel.
 *                  | Master Gain |<-- 18i20 only: Switch per channel
 *                  +------+------+    to select HW or SW gain control.
 *                         |
 *                         |10chn
 *  /--------------\       |
 *  | Analogue     |<------/
 *  | Hardware out |
 *  \--------------/
 * </ditaa>
 *
 */

#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/moduleparam.h>

#include <sound/control.h>
#include <sound/tlv.h>

#include "usbaudio.h"
#include "mixer.h"
#include "helper.h"

#include "mixer_scarlett_gen2.h"

/* device_setup value to enable */
#define SCARLETT2_ENABLE 0x01

/* some gui mixers can't handle negative ctl values */
#define SCARLETT2_VOLUME_BIAS 127

/* mixer range from -80dB to +6dB in 0.5dB steps */
#define SCARLETT2_MIXER_MIN_DB -80
#define SCARLETT2_MIXER_BIAS (-SCARLETT2_MIXER_MIN_DB * 2)
#define SCARLETT2_MIXER_MAX_DB 6
#define SCARLETT2_MIXER_MAX_VALUE \
	((SCARLETT2_MIXER_MAX_DB - SCARLETT2_MIXER_MIN_DB) * 2)

/* map from (dB + 80) * 2 to mixer value
 * for dB in 0 .. 172: int(8192 * pow(10, ((dB - 160) / 2 / 20)))
 */
static const u16 scarlett2_mixer_values[173] = {
	0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2,
	2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 6, 6, 6, 7, 7, 8, 8,
	9, 9, 10, 10, 11, 12, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
	23, 24, 25, 27, 29, 30, 32, 34, 36, 38, 41, 43, 46, 48, 51,
	54, 57, 61, 65, 68, 73, 77, 81, 86, 91, 97, 103, 109, 115,
	122, 129, 137, 145, 154, 163, 173, 183, 194, 205, 217, 230,
	244, 259, 274, 290, 307, 326, 345, 365, 387, 410, 434, 460,
	487, 516, 547, 579, 614, 650, 689, 730, 773, 819, 867, 919,
	973, 1031, 1092, 1157, 1225, 1298, 1375, 1456, 1543, 1634,
	1731, 1833, 1942, 2057, 2179, 2308, 2445, 2590, 2744, 2906,
	3078, 3261, 3454, 3659, 3876, 4105, 4349, 4606, 4879, 5168,
	5475, 5799, 6143, 6507, 6892, 7301, 7733, 8192, 8677, 9191,
	9736, 10313, 10924, 11571, 12257, 12983, 13752, 14567, 15430,
	16345
};

/* Maximum number of analogue outputs */
#define SCARLETT2_ANALOGUE_MAX 10

/* Maximum number of level and pad switches */
#define SCARLETT2_LEVEL_SWITCH_MAX 2
#define SCARLETT2_PAD_SWITCH_MAX 4

/* Maximum number of inputs to the mixer */
#define SCARLETT2_INPUT_MIX_MAX 18

/* Maximum number of outputs from the mixer */
#define SCARLETT2_OUTPUT_MIX_MAX 10

/* Maximum size of the data in the USB mux assignment message:
 * 18 inputs, 20 outputs, 18 matrix inputs, 8 spare
 */
#define SCARLETT2_MUX_MAX 64

/* Number of meters:
 * 18 inputs, 20 outputs, 18 matrix inputs
 */
#define SCARLETT2_NUM_METERS 56

/* Hardware port types:
 * - None (no input to mux)
 * - Analogue I/O
 * - S/PDIF I/O
 * - ADAT I/O
 * - Mixer I/O
 * - PCM I/O
 */
enum {
	SCARLETT2_PORT_TYPE_NONE = 0,
	SCARLETT2_PORT_TYPE_ANALOGUE = 1,
	SCARLETT2_PORT_TYPE_SPDIF = 2,
	SCARLETT2_PORT_TYPE_ADAT = 3,
	SCARLETT2_PORT_TYPE_MIX = 4,
	SCARLETT2_PORT_TYPE_PCM = 5,
	SCARLETT2_PORT_TYPE_COUNT = 6,
};

/* Count of total I/O and number available at each sample rate */
enum {
	SCARLETT2_PORT_IN = 0,
	SCARLETT2_PORT_OUT = 1,
	SCARLETT2_PORT_OUT_44 = 2,
	SCARLETT2_PORT_OUT_88 = 3,
	SCARLETT2_PORT_OUT_176 = 4,
	SCARLETT2_PORT_DIRECTIONS = 5,
};

/* Hardware buttons on the 18i20 */
#define SCARLETT2_BUTTON_MAX 2

static const char *const scarlett2_button_names[SCARLETT2_BUTTON_MAX] = {
	"Mute", "Dim"
};

/* Description of each hardware port type:
 * - id: hardware ID for this port type
 * - num: number of sources/destinations of this port type
 * - src_descr: printf format string for mux input selections
 * - src_num_offset: added to channel number for the fprintf
 * - dst_descr: printf format string for mixer controls
 */
struct scarlett2_ports {
	u16 id;
	int num[SCARLETT2_PORT_DIRECTIONS];
	const char * const src_descr;
	int src_num_offset;
	const char * const dst_descr;
};

struct scarlett2_device_info {
	u8 line_out_hw_vol; /* line out hw volume is sw controlled */
	u8 button_count; /* number of buttons */
	u8 level_input_count; /* inputs with level selectable */
	u8 pad_input_count; /* inputs with pad selectable */
	const char * const line_out_descrs[SCARLETT2_ANALOGUE_MAX];
	struct scarlett2_ports ports[SCARLETT2_PORT_TYPE_COUNT];
};

struct scarlett2_mixer_data {
	struct usb_mixer_interface *mixer;
	struct mutex usb_mutex; /* prevent sending concurrent USB requests */
	struct mutex data_mutex; /* lock access to this data */
	struct delayed_work work;
	const struct scarlett2_device_info *info;
	int num_mux_srcs;
	u16 scarlett2_seq;
	u8 vol_updated;
	u8 master_vol;
	u8 vol[SCARLETT2_ANALOGUE_MAX];
	u8 vol_sw_hw_switch[SCARLETT2_ANALOGUE_MAX];
	u8 level_switch[SCARLETT2_LEVEL_SWITCH_MAX];
	u8 pad_switch[SCARLETT2_PAD_SWITCH_MAX];
	u8 buttons[SCARLETT2_BUTTON_MAX];
	struct snd_kcontrol *master_vol_ctl;
	struct snd_kcontrol *vol_ctls[SCARLETT2_ANALOGUE_MAX];
	struct snd_kcontrol *button_ctls[SCARLETT2_BUTTON_MAX];
	u8 mux[SCARLETT2_MUX_MAX];
	u8 mix[SCARLETT2_INPUT_MIX_MAX * SCARLETT2_OUTPUT_MIX_MAX];
};

/*** Model-specific data ***/

static const struct scarlett2_device_info s6i6_gen2_info = {
	/* The first two analogue inputs can be switched between line
	 * and instrument levels.
	 */
	.level_input_count = 2,

	/* The first two analogue inputs have an optional pad. */
	.pad_input_count = 2,

	.line_out_descrs = {
		"Monitor L",
		"Monitor R",
		"Headphones L",
		"Headphones R",
	},

	.ports = {
		[SCARLETT2_PORT_TYPE_NONE] = {
			.id = 0x000,
			.num = { 1, 0, 8, 8, 8 },
			.src_descr = "Off",
			.src_num_offset = 0,
		},
		[SCARLETT2_PORT_TYPE_ANALOGUE] = {
			.id = 0x080,
			.num = { 4, 4, 4, 4, 4 },
			.src_descr = "Analogue %d",
			.src_num_offset = 1,
			.dst_descr = "Analogue Output %02d Playback"
		},
		[SCARLETT2_PORT_TYPE_SPDIF] = {
			.id = 0x180,
			.num = { 2, 2, 2, 2, 2 },
			.src_descr = "S/PDIF %d",
			.src_num_offset = 1,
			.dst_descr = "S/PDIF Output %d Playback"
		},
		[SCARLETT2_PORT_TYPE_MIX] = {
			.id = 0x300,
			.num = { 10, 18, 18, 18, 18 },
			.src_descr = "Mix %c",
			.src_num_offset = 65,
			.dst_descr = "Mixer Input %02d Capture"
		},
		[SCARLETT2_PORT_TYPE_PCM] = {
			.id = 0x600,
			.num = { 6, 6, 6, 6, 6 },
			.src_descr = "PCM %d",
			.src_num_offset = 1,
			.dst_descr = "PCM %02d Capture"
		},
	},
};

static const struct scarlett2_device_info s18i8_gen2_info = {
	/* The first two analogue inputs can be switched between line
	 * and instrument levels.
	 */
	.level_input_count = 2,

	/* The first four analogue inputs have an optional pad. */
	.pad_input_count = 4,

	.line_out_descrs = {
		"Monitor L",
		"Monitor R",
		"Headphones 1 L",
		"Headphones 1 R",
		"Headphones 2 L",
		"Headphones 2 R",
	},

	.ports = {
		[SCARLETT2_PORT_TYPE_NONE] = {
			.id = 0x000,
			.num = { 1, 0, 8, 8, 4 },
			.src_descr = "Off",
			.src_num_offset = 0,
		},
		[SCARLETT2_PORT_TYPE_ANALOGUE] = {
			.id = 0x080,
			.num = { 8, 6, 6, 6, 6 },
			.src_descr = "Analogue %d",
			.src_num_offset = 1,
			.dst_descr = "Analogue Output %02d Playback"
		},
		[SCARLETT2_PORT_TYPE_SPDIF] = {
			.id = 0x180,
			/* S/PDIF outputs aren't available at 192kHz
			 * but are included in the USB mux I/O
			 * assignment message anyway
			 */
			.num = { 2, 2, 2, 2, 2 },
			.src_descr = "S/PDIF %d",
			.src_num_offset = 1,
			.dst_descr = "S/PDIF Output %d Playback"
		},
		[SCARLETT2_PORT_TYPE_ADAT] = {
			.id = 0x200,
			.num = { 8, 0, 0, 0, 0 },
			.src_descr = "ADAT %d",
			.src_num_offset = 1,
		},
		[SCARLETT2_PORT_TYPE_MIX] = {
			.id = 0x300,
			.num = { 10, 18, 18, 18, 18 },
			.src_descr = "Mix %c",
			.src_num_offset = 65,
			.dst_descr = "Mixer Input %02d Capture"
		},
		[SCARLETT2_PORT_TYPE_PCM] = {
			.id = 0x600,
			.num = { 20, 18, 18, 14, 10 },
			.src_descr = "PCM %d",
			.src_num_offset = 1,
			.dst_descr = "PCM %02d Capture"
		},
	},
};

static const struct scarlett2_device_info s18i20_gen2_info = {
	/* The analogue line outputs on the 18i20 can be switched
	 * between software and hardware volume control
	 */
	.line_out_hw_vol = 1,

	/* Mute and dim buttons */
	.button_count = 2,

	.line_out_descrs = {
		"Monitor L",
		"Monitor R",
		NULL,
		NULL,
		NULL,
		NULL,
		"Headphones 1 L",
		"Headphones 1 R",
		"Headphones 2 L",
		"Headphones 2 R",
	},

	.ports = {
		[SCARLETT2_PORT_TYPE_NONE] = {
			.id = 0x000,
			.num = { 1, 0, 8, 8, 6 },
			.src_descr = "Off",
			.src_num_offset = 0,
		},
		[SCARLETT2_PORT_TYPE_ANALOGUE] = {
			.id = 0x080,
			.num = { 8, 10, 10, 10, 10 },
			.src_descr = "Analogue %d",
			.src_num_offset = 1,
			.dst_descr = "Analogue Output %02d Playback"
		},
		[SCARLETT2_PORT_TYPE_SPDIF] = {
			/* S/PDIF outputs aren't available at 192kHz
			 * but are included in the USB mux I/O
			 * assignment message anyway
			 */
			.id = 0x180,
			.num = { 2, 2, 2, 2, 2 },
			.src_descr = "S/PDIF %d",
			.src_num_offset = 1,
			.dst_descr = "S/PDIF Output %d Playback"
		},
		[SCARLETT2_PORT_TYPE_ADAT] = {
			.id = 0x200,
			.num = { 8, 8, 8, 4, 0 },
			.src_descr = "ADAT %d",
			.src_num_offset = 1,
			.dst_descr = "ADAT Output %d Playback"
		},
		[SCARLETT2_PORT_TYPE_MIX] = {
			.id = 0x300,
			.num = { 10, 18, 18, 18, 18 },
			.src_descr = "Mix %c",
			.src_num_offset = 65,
			.dst_descr = "Mixer Input %02d Capture"
		},
		[SCARLETT2_PORT_TYPE_PCM] = {
			.id = 0x600,
			.num = { 20, 18, 18, 14, 10 },
			.src_descr = "PCM %d",
			.src_num_offset = 1,
			.dst_descr = "PCM %02d Capture"
		},
	},
};

/* get the starting port index number for a given port type/direction */
static int scarlett2_get_port_start_num(const struct scarlett2_ports *ports,
					int direction, int port_type)
{
	int i, num = 0;

	for (i = 0; i < port_type; i++)
		num += ports[i].num[direction];

	return num;
}

/*** USB Interactions ***/

/* Vendor-Specific Interface, Endpoint, MaxPacketSize, Interval */
#define SCARLETT2_USB_VENDOR_SPECIFIC_INTERFACE 5
#define SCARLETT2_USB_INTERRUPT_ENDPOINT 4
#define SCARLETT2_USB_INTERRUPT_MAX_DATA 64
#define SCARLETT2_USB_INTERRUPT_INTERVAL 3

/* Interrupt flags for volume and mute/dim button changes */
#define SCARLETT2_USB_INTERRUPT_VOL_CHANGE 0x400000
#define SCARLETT2_USB_INTERRUPT_BUTTON_CHANGE 0x200000

/* Commands for sending/receiving requests/responses */
#define SCARLETT2_USB_VENDOR_SPECIFIC_CMD_REQ 2
#define SCARLETT2_USB_VENDOR_SPECIFIC_CMD_RESP 3

#define SCARLETT2_USB_INIT_SEQ 0x00000000
#define SCARLETT2_USB_GET_METER_LEVELS 0x00001001
#define SCARLETT2_USB_SET_MIX 0x00002002
#define SCARLETT2_USB_SET_MUX 0x00003002
#define SCARLETT2_USB_GET_DATA 0x00800000
#define SCARLETT2_USB_SET_DATA 0x00800001
#define SCARLETT2_USB_DATA_CMD 0x00800002
#define SCARLETT2_USB_CONFIG_SAVE 6

#define SCARLETT2_USB_VOLUME_STATUS_OFFSET 0x31
#define SCARLETT2_USB_METER_LEVELS_GET_MAGIC 1

/* volume status is read together (matches scarlett2_config_items[]) */
struct scarlett2_usb_volume_status {
	/* mute & dim buttons */
	u8 buttons[SCARLETT2_BUTTON_MAX];

	u8 pad1;

	/* software volume setting */
	s16 sw_vol[SCARLETT2_ANALOGUE_MAX];

	/* actual volume of output inc. dim (-18dB) */
	s16 hw_vol[SCARLETT2_ANALOGUE_MAX];

	u8 pad2[SCARLETT2_ANALOGUE_MAX];

	/* sw (0) or hw (1) controlled */
	u8 sw_hw_switch[SCARLETT2_ANALOGUE_MAX];

	u8 pad3[6];

	/* front panel volume knob */
	s16 master_vol;
} __packed;

/* Configuration parameters that can be read and written */
enum {
	SCARLETT2_CONFIG_BUTTONS = 0,
	SCARLETT2_CONFIG_LINE_OUT_VOLUME = 1,
	SCARLETT2_CONFIG_SW_HW_SWITCH = 2,
	SCARLETT2_CONFIG_LEVEL_SWITCH = 3,
	SCARLETT2_CONFIG_PAD_SWITCH = 4,
	SCARLETT2_CONFIG_COUNT = 5
};

/* Location, size, and activation command number for the configuration
 * parameters
 */
struct scarlett2_config {
	u8 offset;
	u8 size;
	u8 activate;
};

static const struct scarlett2_config
		scarlett2_config_items[SCARLETT2_CONFIG_COUNT] = {
	/* Mute/Dim Buttons */
	{
		.offset = 0x31,
		.size = 1,
		.activate = 2
	},

	/* Line Out Volume */
	{
		.offset = 0x34,
		.size = 2,
		.activate = 1
	},

	/* SW/HW Volume Switch */
	{
		.offset = 0x66,
		.size = 1,
		.activate = 3
	},

	/* Level Switch */
	{
		.offset = 0x7c,
		.size = 1,
		.activate = 7
	},

	/* Pad Switch */
	{
		.offset = 0x84,
		.size = 1,
		.activate = 8
	}
};

/* proprietary request/response format */
struct scarlett2_usb_packet {
	__le32 cmd;
	__le16 size;
	__le16 seq;
	__le32 error;
	__le32 pad;
	u8 data[];
};

#define SCARLETT2_USB_PACKET_LEN (sizeof(struct scarlett2_usb_packet))

static void scarlett2_fill_request_header(struct scarlett2_mixer_data *private,
					  struct scarlett2_usb_packet *req,
					  u32 cmd, u16 req_size)
{
	/* sequence must go up by 1 for each request */
	u16 seq = private->scarlett2_seq++;

	req->cmd = cpu_to_le32(cmd);
	req->size = cpu_to_le16(req_size);
	req->seq = cpu_to_le16(seq);
	req->error = 0;
	req->pad = 0;
}

/* Send a proprietary format request to the Scarlett interface */
static int scarlett2_usb(
	struct usb_mixer_interface *mixer, u32 cmd,
	void *req_data, u16 req_size, void *resp_data, u16 resp_size)
{
	struct scarlett2_mixer_data *private = mixer->private_data;
	u16 req_buf_size = sizeof(struct scarlett2_usb_packet) + req_size;
	u16 resp_buf_size = sizeof(struct scarlett2_usb_packet) + resp_size;
	struct scarlett2_usb_packet *req = NULL, *resp = NULL;
	int err = 0;

	req = kmalloc(req_buf_size, GFP_KERNEL);
	if (!req) {
		err = -ENOMEM;
		goto error;
	}

	resp = kmalloc(resp_buf_size, GFP_KERNEL);
	if (!resp) {
		err = -ENOMEM;
		goto error;
	}

	mutex_lock(&private->usb_mutex);

	/* build request message and send it */

	scarlett2_fill_request_header(private, req, cmd, req_size);

	if (req_size)
		memcpy(req->data, req_data, req_size);

	err = snd_usb_ctl_msg(mixer->chip->dev,
			usb_sndctrlpipe(mixer->chip->dev, 0),
			SCARLETT2_USB_VENDOR_SPECIFIC_CMD_REQ,
			USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
			0,
			SCARLETT2_USB_VENDOR_SPECIFIC_INTERFACE,
			req,
			req_buf_size);

	if (err != req_buf_size) {
		usb_audio_err(
			mixer->chip,
			"Scarlett Gen 2 USB request result cmd %x was %d\n",
			cmd, err);
		err = -EINVAL;
		goto unlock;
	}

	/* send a second message to get the response */

	err = snd_usb_ctl_msg(mixer->chip->dev,
			usb_rcvctrlpipe(mixer->chip->dev, 0),
			SCARLETT2_USB_VENDOR_SPECIFIC_CMD_RESP,
			USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
			0,
			SCARLETT2_USB_VENDOR_SPECIFIC_INTERFACE,
			resp,
			resp_buf_size);

	/* validate the response */

	if (err != resp_buf_size) {
		usb_audio_err(
			mixer->chip,
			"Scarlett Gen 2 USB response result cmd %x was %d\n",
			cmd, err);
		err = -EINVAL;
		goto unlock;
	}

	if (resp->cmd != req->cmd ||
	    resp->seq != req->seq ||
	    resp_size != le16_to_cpu(resp->size) ||
	    resp->error ||
	    resp->pad) {
		usb_audio_err(
			mixer->chip,
			"Scarlett Gen 2 USB invalid response; "
			   "cmd tx/rx %d/%d seq %d/%d size %d/%d "
			   "error %d pad %d\n",
			le32_to_cpu(req->cmd), le32_to_cpu(resp->cmd),
			le16_to_cpu(req->seq), le16_to_cpu(resp->seq),
			resp_size, le16_to_cpu(resp->size),
			le32_to_cpu(resp->error),
			le32_to_cpu(resp->pad));
		err = -EINVAL;
		goto unlock;
	}

	if (resp_size > 0)
		memcpy(resp_data, resp->data, resp_size);

unlock:
	mutex_unlock(&private->usb_mutex);
error:
	kfree(req);
	kfree(resp);
	return err;
}

/* Send SCARLETT2_USB_DATA_CMD SCARLETT2_USB_CONFIG_SAVE */
static void scarlett2_config_save(struct usb_mixer_interface *mixer)
{
	__le32 req = cpu_to_le32(SCARLETT2_USB_CONFIG_SAVE);

	scarlett2_usb(mixer, SCARLETT2_USB_DATA_CMD,
		      &req, sizeof(u32),
		      NULL, 0);
}

/* Delayed work to save config */
static void scarlett2_config_save_work(struct work_struct *work)
{
	struct scarlett2_mixer_data *private =
		container_of(work, struct scarlett2_mixer_data, work.work);

	scarlett2_config_save(private->mixer);
}

/* Send a USB message to set a configuration parameter (volume level,
 * sw/hw volume switch, line/inst level switch, or pad switch)
 */
static int scarlett2_usb_set_config(
	struct usb_mixer_interface *mixer,
	int config_item_num, int index, int value)
{
	const struct scarlett2_config config_item =
	       scarlett2_config_items[config_item_num];
	struct {
		__le32 offset;
		__le32 bytes;
		__le32 value;
	} __packed req;
	__le32 req2;
	int err;
	struct scarlett2_mixer_data *private = mixer->private_data;

	/* Cancel any pending NVRAM save */
	cancel_delayed_work_sync(&private->work);

	/* Send the configuration parameter data */
	req.offset = cpu_to_le32(config_item.offset + index * config_item.size);
	req.bytes = cpu_to_le32(config_item.size);
	req.value = cpu_to_le32(value);
	err = scarlett2_usb(mixer, SCARLETT2_USB_SET_DATA,
			    &req, sizeof(u32) * 2 + config_item.size,
			    NULL, 0);
	if (err < 0)
		return err;

	/* Activate the change */
	req2 = cpu_to_le32(config_item.activate);
	err = scarlett2_usb(mixer, SCARLETT2_USB_DATA_CMD,
			    &req2, sizeof(req2), NULL, 0);
	if (err < 0)
		return err;

	/* Schedule the change to be written to NVRAM */
	schedule_delayed_work(&private->work, msecs_to_jiffies(2000));

	return 0;
}

/* Send a USB message to get data; result placed in *buf */
static int scarlett2_usb_get(
	struct usb_mixer_interface *mixer,
	int offset, void *buf, int size)
{
	struct {
		__le32 offset;
		__le32 size;
	} __packed req;

	req.offset = cpu_to_le32(offset);
	req.size = cpu_to_le32(size);
	return scarlett2_usb(mixer, SCARLETT2_USB_GET_DATA,
			     &req, sizeof(req), buf, size);
}

/* Send a USB message to get configuration parameters; result placed in *buf */
static int scarlett2_usb_get_config(
	struct usb_mixer_interface *mixer,
	int config_item_num, int count, void *buf)
{
	const struct scarlett2_config config_item =
	       scarlett2_config_items[config_item_num];
	int size = config_item.size * count;

	return scarlett2_usb_get(mixer, config_item.offset, buf, size);
}

/* Send a USB message to get volume status; result placed in *buf */
static int scarlett2_usb_get_volume_status(
	struct usb_mixer_interface *mixer,
	struct scarlett2_usb_volume_status *buf)
{
	return scarlett2_usb_get(mixer, SCARLETT2_USB_VOLUME_STATUS_OFFSET,
				 buf, sizeof(*buf));
}

/* Send a USB message to set the volumes for all inputs of one mix
 * (values obtained from private->mix[])
 */
static int scarlett2_usb_set_mix(struct usb_mixer_interface *mixer,
				     int mix_num)
{
	struct scarlett2_mixer_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;

	struct {
		__le16 mix_num;
		__le16 data[SCARLETT2_INPUT_MIX_MAX];
	} __packed req;

	int i, j;
	int num_mixer_in =
		info->ports[SCARLETT2_PORT_TYPE_MIX].num[SCARLETT2_PORT_OUT];

	req.mix_num = cpu_to_le16(mix_num);

	for (i = 0, j = mix_num * num_mixer_in; i < num_mixer_in; i++, j++)
		req.data[i] = cpu_to_le16(
			scarlett2_mixer_values[private->mix[j]]
		);

	return scarlett2_usb(mixer, SCARLETT2_USB_SET_MIX,
			     &req, (num_mixer_in + 1) * sizeof(u16),
			     NULL, 0);
}

/* Convert a port number index (per info->ports) to a hardware ID */
static u32 scarlett2_mux_src_num_to_id(const struct scarlett2_ports *ports,
				       int num)
{
	int port_type;

	for (port_type = 0;
	     port_type < SCARLETT2_PORT_TYPE_COUNT;
	     port_type++) {
		if (num < ports[port_type].num[SCARLETT2_PORT_IN])
			return ports[port_type].id | num;
		num -= ports[port_type].num[SCARLETT2_PORT_IN];
	}

	/* Oops */
	return 0;
}

/* Send USB messages to set mux inputs */
static int scarlett2_usb_set_mux(struct usb_mixer_interface *mixer)
{
	struct scarlett2_mixer_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	const struct scarlett2_ports *ports = info->ports;
	int rate, port_dir_rate;

	static const int assignment_order[SCARLETT2_PORT_TYPE_COUNT] = {
		SCARLETT2_PORT_TYPE_PCM,
		SCARLETT2_PORT_TYPE_ANALOGUE,
		SCARLETT2_PORT_TYPE_SPDIF,
		SCARLETT2_PORT_TYPE_ADAT,
		SCARLETT2_PORT_TYPE_MIX,
		SCARLETT2_PORT_TYPE_NONE,
	};

	struct {
		__le16 pad;
		__le16 num;
		__le32 data[SCARLETT2_MUX_MAX];
	} __packed req;

	req.pad = 0;

	/* mux settings for each rate */
	for (rate = 0, port_dir_rate = SCARLETT2_PORT_OUT_44;
	     port_dir_rate <= SCARLETT2_PORT_OUT_176;
	     rate++, port_dir_rate++) {
		int order_num, i, err;

		req.num = cpu_to_le16(rate);

		for (order_num = 0, i = 0;
		     order_num < SCARLETT2_PORT_TYPE_COUNT;
		     order_num++) {
			int port_type = assignment_order[order_num];
			int j = scarlett2_get_port_start_num(ports,
							     SCARLETT2_PORT_OUT,
							     port_type);
			int port_id = ports[port_type].id;
			int channel;

			for (channel = 0;
			     channel < ports[port_type].num[port_dir_rate];
			     channel++, i++, j++)
				/* lower 12 bits for the destination and
				 * next 12 bits for the source
				 */
				req.data[i] = !port_id
					? 0
					: cpu_to_le32(
						port_id |
						channel |
						scarlett2_mux_src_num_to_id(
							ports, private->mux[j]
						) << 12
					  );

			/* skip private->mux[j] entries not output */
			j += ports[port_type].num[SCARLETT2_PORT_OUT] -
			     ports[port_type].num[port_dir_rate];
		}

		err = scarlett2_usb(mixer, SCARLETT2_USB_SET_MUX,
				    &req, (i + 1) * sizeof(u32),
				    NULL, 0);
		if (err < 0)
			return err;
	}

	return 0;
}

/* Send USB message to get meter levels */
static int scarlett2_usb_get_meter_levels(struct usb_mixer_interface *mixer,
					  u16 *levels)
{
	struct {
		__le16 pad;
		__le16 num_meters;
		__le32 magic;
	} __packed req;
	u32 resp[SCARLETT2_NUM_METERS];
	int i, err;

	req.pad = 0;
	req.num_meters = cpu_to_le16(SCARLETT2_NUM_METERS);
	req.magic = cpu_to_le32(SCARLETT2_USB_METER_LEVELS_GET_MAGIC);
	err = scarlett2_usb(mixer, SCARLETT2_USB_GET_METER_LEVELS,
			    &req, sizeof(req), resp, sizeof(resp));
	if (err < 0)
		return err;

	/* copy, convert to u16 */
	for (i = 0; i < SCARLETT2_NUM_METERS; i++)
		levels[i] = resp[i];

	return 0;
}

/*** Control Functions ***/

/* helper function to create a new control */
static int scarlett2_add_new_ctl(struct usb_mixer_interface *mixer,
				 const struct snd_kcontrol_new *ncontrol,
				 int index, int channels, const char *name,
				 struct snd_kcontrol **kctl_return)
{
	struct snd_kcontrol *kctl;
	struct usb_mixer_elem_info *elem;
	int err;

	elem = kzalloc(sizeof(*elem), GFP_KERNEL);
	if (!elem)
		return -ENOMEM;

	elem->head.mixer = mixer;
	elem->control = index;
	elem->head.id = index;
	elem->channels = channels;

	kctl = snd_ctl_new1(ncontrol, elem);
	if (!kctl) {
		kfree(elem);
		return -ENOMEM;
	}
	kctl->private_free = snd_usb_mixer_elem_free;

	strscpy(kctl->id.name, name, sizeof(kctl->id.name));

	err = snd_usb_mixer_add_control(&elem->head, kctl);
	if (err < 0)
		return err;

	if (kctl_return)
		*kctl_return = kctl;

	return 0;
}

/*** Analogue Line Out Volume Controls ***/

/* Update hardware volume controls after receiving notification that
 * they have changed
 */
static int scarlett2_update_volumes(struct usb_mixer_interface *mixer)
{
	struct scarlett2_mixer_data *private = mixer->private_data;
	const struct scarlett2_ports *ports = private->info->ports;
	struct scarlett2_usb_volume_status volume_status;
	int num_line_out =
		ports[SCARLETT2_PORT_TYPE_ANALOGUE].num[SCARLETT2_PORT_OUT];
	int err, i;

	private->vol_updated = 0;

	err = scarlett2_usb_get_volume_status(mixer, &volume_status);
	if (err < 0)
		return err;

	private->master_vol = clamp(
		volume_status.master_vol + SCARLETT2_VOLUME_BIAS,
		0, SCARLETT2_VOLUME_BIAS);

	for (i = 0; i < num_line_out; i++) {
		if (private->vol_sw_hw_switch[i])
			private->vol[i] = private->master_vol;
	}

	for (i = 0; i < private->info->button_count; i++)
		private->buttons[i] = !!volume_status.buttons[i];

	return 0;
}

static int scarlett2_volume_ctl_info(struct snd_kcontrol *kctl,
				     struct snd_ctl_elem_info *uinfo)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = elem->channels;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = SCARLETT2_VOLUME_BIAS;
	uinfo->value.integer.step = 1;
	return 0;
}

static int scarlett2_master_volume_ctl_get(struct snd_kcontrol *kctl,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_mixer_data *private = mixer->private_data;

	if (private->vol_updated) {
		mutex_lock(&private->data_mutex);
		scarlett2_update_volumes(mixer);
		mutex_unlock(&private->data_mutex);
	}

	ucontrol->value.integer.value[0] = private->master_vol;
	return 0;
}

static int scarlett2_volume_ctl_get(struct snd_kcontrol *kctl,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_mixer_data *private = mixer->private_data;
	int index = elem->control;

	if (private->vol_updated) {
		mutex_lock(&private->data_mutex);
		scarlett2_update_volumes(mixer);
		mutex_unlock(&private->data_mutex);
	}

	ucontrol->value.integer.value[0] = private->vol[index];
	return 0;
}

static int scarlett2_volume_ctl_put(struct snd_kcontrol *kctl,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_mixer_data *private = mixer->private_data;
	int index = elem->control;
	int oval, val, err = 0;

	mutex_lock(&private->data_mutex);

	oval = private->vol[index];
	val = ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->vol[index] = val;
	err = scarlett2_usb_set_config(mixer, SCARLETT2_CONFIG_LINE_OUT_VOLUME,
				       index, val - SCARLETT2_VOLUME_BIAS);
	if (err == 0)
		err = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const DECLARE_TLV_DB_MINMAX(
	db_scale_scarlett2_gain, -SCARLETT2_VOLUME_BIAS * 100, 0
);

static const struct snd_kcontrol_new scarlett2_master_volume_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READ |
		  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.name = "",
	.info = scarlett2_volume_ctl_info,
	.get  = scarlett2_master_volume_ctl_get,
	.private_value = 0, /* max value */
	.tlv = { .p = db_scale_scarlett2_gain }
};

static const struct snd_kcontrol_new scarlett2_line_out_volume_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
		  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.name = "",
	.info = scarlett2_volume_ctl_info,
	.get  = scarlett2_volume_ctl_get,
	.put  = scarlett2_volume_ctl_put,
	.private_value = 0, /* max value */
	.tlv = { .p = db_scale_scarlett2_gain }
};

/*** HW/SW Volume Switch Controls ***/

static int scarlett2_sw_hw_enum_ctl_info(struct snd_kcontrol *kctl,
					 struct snd_ctl_elem_info *uinfo)
{
	static const char *const values[2] = {
		"SW", "HW"
	};

	return snd_ctl_enum_info(uinfo, 1, 2, values);
}

static int scarlett2_sw_hw_enum_ctl_get(struct snd_kcontrol *kctl,
					struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct scarlett2_mixer_data *private = elem->head.mixer->private_data;

	ucontrol->value.enumerated.item[0] =
		private->vol_sw_hw_switch[elem->control];
	return 0;
}

static int scarlett2_sw_hw_enum_ctl_put(struct snd_kcontrol *kctl,
					struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_mixer_data *private = mixer->private_data;

	int index = elem->control;
	int oval, val, err = 0;

	mutex_lock(&private->data_mutex);

	oval = private->vol_sw_hw_switch[index];
	val = !!ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->vol_sw_hw_switch[index] = val;

	/* Change access mode to RO (hardware controlled volume)
	 * or RW (software controlled volume)
	 */
	if (val)
		private->vol_ctls[index]->vd[0].access &=
			~SNDRV_CTL_ELEM_ACCESS_WRITE;
	else
		private->vol_ctls[index]->vd[0].access |=
			SNDRV_CTL_ELEM_ACCESS_WRITE;

	/* Reset volume to master volume */
	private->vol[index] = private->master_vol;

	/* Set SW volume to current HW volume */
	err = scarlett2_usb_set_config(
		mixer, SCARLETT2_CONFIG_LINE_OUT_VOLUME,
		index, private->master_vol - SCARLETT2_VOLUME_BIAS);
	if (err < 0)
		goto unlock;

	/* Notify of RO/RW change */
	snd_ctl_notify(mixer->chip->card, SNDRV_CTL_EVENT_MASK_INFO,
		       &private->vol_ctls[index]->id);

	/* Send SW/HW switch change to the device */
	err = scarlett2_usb_set_config(mixer, SCARLETT2_CONFIG_SW_HW_SWITCH,
				       index, val);

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_sw_hw_enum_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = scarlett2_sw_hw_enum_ctl_info,
	.get  = scarlett2_sw_hw_enum_ctl_get,
	.put  = scarlett2_sw_hw_enum_ctl_put,
};

/*** Line Level/Instrument Level Switch Controls ***/

static int scarlett2_level_enum_ctl_info(struct snd_kcontrol *kctl,
					 struct snd_ctl_elem_info *uinfo)
{
	static const char *const values[2] = {
		"Line", "Inst"
	};

	return snd_ctl_enum_info(uinfo, 1, 2, values);
}

static int scarlett2_level_enum_ctl_get(struct snd_kcontrol *kctl,
					struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct scarlett2_mixer_data *private = elem->head.mixer->private_data;

	ucontrol->value.enumerated.item[0] =
		private->level_switch[elem->control];
	return 0;
}

static int scarlett2_level_enum_ctl_put(struct snd_kcontrol *kctl,
					struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_mixer_data *private = mixer->private_data;

	int index = elem->control;
	int oval, val, err = 0;

	mutex_lock(&private->data_mutex);

	oval = private->level_switch[index];
	val = !!ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->level_switch[index] = val;

	/* Send switch change to the device */
	err = scarlett2_usb_set_config(mixer, SCARLETT2_CONFIG_LEVEL_SWITCH,
				       index, val);

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_level_enum_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = scarlett2_level_enum_ctl_info,
	.get  = scarlett2_level_enum_ctl_get,
	.put  = scarlett2_level_enum_ctl_put,
};

/*** Pad Switch Controls ***/

static int scarlett2_pad_ctl_get(struct snd_kcontrol *kctl,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct scarlett2_mixer_data *private = elem->head.mixer->private_data;

	ucontrol->value.enumerated.item[0] =
		private->pad_switch[elem->control];
	return 0;
}

static int scarlett2_pad_ctl_put(struct snd_kcontrol *kctl,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_mixer_data *private = mixer->private_data;

	int index = elem->control;
	int oval, val, err = 0;

	mutex_lock(&private->data_mutex);

	oval = private->pad_switch[index];
	val = !!ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->pad_switch[index] = val;

	/* Send switch change to the device */
	err = scarlett2_usb_set_config(mixer, SCARLETT2_CONFIG_PAD_SWITCH,
				       index, val);

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_pad_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = snd_ctl_boolean_mono_info,
	.get  = scarlett2_pad_ctl_get,
	.put  = scarlett2_pad_ctl_put,
};

/*** Mute/Dim Controls ***/

static int scarlett2_button_ctl_get(struct snd_kcontrol *kctl,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_mixer_data *private = mixer->private_data;

	if (private->vol_updated) {
		mutex_lock(&private->data_mutex);
		scarlett2_update_volumes(mixer);
		mutex_unlock(&private->data_mutex);
	}

	ucontrol->value.enumerated.item[0] = private->buttons[elem->control];
	return 0;
}

static int scarlett2_button_ctl_put(struct snd_kcontrol *kctl,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_mixer_data *private = mixer->private_data;

	int index = elem->control;
	int oval, val, err = 0;

	mutex_lock(&private->data_mutex);

	oval = private->buttons[index];
	val = !!ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->buttons[index] = val;

	/* Send switch change to the device */
	err = scarlett2_usb_set_config(mixer, SCARLETT2_CONFIG_BUTTONS,
				       index, val);

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_button_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = snd_ctl_boolean_mono_info,
	.get  = scarlett2_button_ctl_get,
	.put  = scarlett2_button_ctl_put
};

/*** Create the analogue output controls ***/

static int scarlett2_add_line_out_ctls(struct usb_mixer_interface *mixer)
{
	struct scarlett2_mixer_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	const struct scarlett2_ports *ports = info->ports;
	int num_line_out =
		ports[SCARLETT2_PORT_TYPE_ANALOGUE].num[SCARLETT2_PORT_OUT];
	int err, i;
	char s[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];

	/* Add R/O HW volume control */
	if (info->line_out_hw_vol) {
		snprintf(s, sizeof(s), "Master HW Playback Volume");
		err = scarlett2_add_new_ctl(mixer,
					    &scarlett2_master_volume_ctl,
					    0, 1, s, &private->master_vol_ctl);
		if (err < 0)
			return err;
	}

	/* Add volume controls */
	for (i = 0; i < num_line_out; i++) {

		/* Fader */
		if (info->line_out_descrs[i])
			snprintf(s, sizeof(s),
				 "Line %02d (%s) Playback Volume",
				 i + 1, info->line_out_descrs[i]);
		else
			snprintf(s, sizeof(s),
				 "Line %02d Playback Volume",
				 i + 1);
		err = scarlett2_add_new_ctl(mixer,
					    &scarlett2_line_out_volume_ctl,
					    i, 1, s, &private->vol_ctls[i]);
		if (err < 0)
			return err;

		/* Make the fader read-only if the SW/HW switch is set to HW */
		if (private->vol_sw_hw_switch[i])
			private->vol_ctls[i]->vd[0].access &=
				~SNDRV_CTL_ELEM_ACCESS_WRITE;

		/* SW/HW Switch */
		if (info->line_out_hw_vol) {
			snprintf(s, sizeof(s),
				 "Line Out %02d Volume Control Playback Enum",
				 i + 1);
			err = scarlett2_add_new_ctl(mixer,
						    &scarlett2_sw_hw_enum_ctl,
						    i, 1, s, NULL);
			if (err < 0)
				return err;
		}
	}

	/* Add HW button controls */
	for (i = 0; i < private->info->button_count; i++) {
		err = scarlett2_add_new_ctl(mixer, &scarlett2_button_ctl,
					    i, 1, scarlett2_button_names[i],
					    &private->button_ctls[i]);
		if (err < 0)
			return err;
	}

	return 0;
}

/*** Create the analogue input controls ***/

static int scarlett2_add_line_in_ctls(struct usb_mixer_interface *mixer)
{
	struct scarlett2_mixer_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int err, i;
	char s[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];

	/* Add input level (line/inst) controls */
	for (i = 0; i < info->level_input_count; i++) {
		snprintf(s, sizeof(s), "Line In %d Level Capture Enum", i + 1);
		err = scarlett2_add_new_ctl(mixer, &scarlett2_level_enum_ctl,
					    i, 1, s, NULL);
		if (err < 0)
			return err;
	}

	/* Add input pad controls */
	for (i = 0; i < info->pad_input_count; i++) {
		snprintf(s, sizeof(s), "Line In %d Pad Capture Switch", i + 1);
		err = scarlett2_add_new_ctl(mixer, &scarlett2_pad_ctl,
					    i, 1, s, NULL);
		if (err < 0)
			return err;
	}

	return 0;
}

/*** Mixer Volume Controls ***/

static int scarlett2_mixer_ctl_info(struct snd_kcontrol *kctl,
				    struct snd_ctl_elem_info *uinfo)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = elem->channels;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = SCARLETT2_MIXER_MAX_VALUE;
	uinfo->value.integer.step = 1;
	return 0;
}

static int scarlett2_mixer_ctl_get(struct snd_kcontrol *kctl,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct scarlett2_mixer_data *private = elem->head.mixer->private_data;

	ucontrol->value.integer.value[0] = private->mix[elem->control];
	return 0;
}

static int scarlett2_mixer_ctl_put(struct snd_kcontrol *kctl,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_mixer_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	const struct scarlett2_ports *ports = info->ports;
	int oval, val, num_mixer_in, mix_num, err = 0;

	mutex_lock(&private->data_mutex);

	oval = private->mix[elem->control];
	val = ucontrol->value.integer.value[0];
	num_mixer_in = ports[SCARLETT2_PORT_TYPE_MIX].num[SCARLETT2_PORT_OUT];
	mix_num = elem->control / num_mixer_in;

	if (oval == val)
		goto unlock;

	private->mix[elem->control] = val;
	err = scarlett2_usb_set_mix(mixer, mix_num);
	if (err == 0)
		err = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const DECLARE_TLV_DB_MINMAX(
	db_scale_scarlett2_mixer,
	SCARLETT2_MIXER_MIN_DB * 100,
	SCARLETT2_MIXER_MAX_DB * 100
);

static const struct snd_kcontrol_new scarlett2_mixer_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
		  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.name = "",
	.info = scarlett2_mixer_ctl_info,
	.get  = scarlett2_mixer_ctl_get,
	.put  = scarlett2_mixer_ctl_put,
	.private_value = SCARLETT2_MIXER_MAX_DB, /* max value */
	.tlv = { .p = db_scale_scarlett2_mixer }
};

static int scarlett2_add_mixer_ctls(struct usb_mixer_interface *mixer)
{
	struct scarlett2_mixer_data *private = mixer->private_data;
	const struct scarlett2_ports *ports = private->info->ports;
	int err, i, j;
	int index;
	char s[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];

	int num_inputs = ports[SCARLETT2_PORT_TYPE_MIX].num[SCARLETT2_PORT_OUT];
	int num_outputs = ports[SCARLETT2_PORT_TYPE_MIX].num[SCARLETT2_PORT_IN];

	for (i = 0, index = 0; i < num_outputs; i++) {
		for (j = 0; j < num_inputs; j++, index++) {
			snprintf(s, sizeof(s),
				 "Mix %c Input %02d Playback Volume",
				 'A' + i, j + 1);
			err = scarlett2_add_new_ctl(mixer, &scarlett2_mixer_ctl,
						    index, 1, s, NULL);
			if (err < 0)
				return err;
		}
	}

	return 0;
}

/*** Mux Source Selection Controls ***/

static int scarlett2_mux_src_enum_ctl_info(struct snd_kcontrol *kctl,
					   struct snd_ctl_elem_info *uinfo)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct scarlett2_mixer_data *private = elem->head.mixer->private_data;
	const struct scarlett2_ports *ports = private->info->ports;
	unsigned int item = uinfo->value.enumerated.item;
	int items = private->num_mux_srcs;
	int port_type;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = elem->channels;
	uinfo->value.enumerated.items = items;

	if (item >= items)
		item = uinfo->value.enumerated.item = items - 1;

	for (port_type = 0;
	     port_type < SCARLETT2_PORT_TYPE_COUNT;
	     port_type++) {
		if (item < ports[port_type].num[SCARLETT2_PORT_IN]) {
			sprintf(uinfo->value.enumerated.name,
				ports[port_type].src_descr,
				item + ports[port_type].src_num_offset);
			return 0;
		}
		item -= ports[port_type].num[SCARLETT2_PORT_IN];
	}

	return -EINVAL;
}

static int scarlett2_mux_src_enum_ctl_get(struct snd_kcontrol *kctl,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct scarlett2_mixer_data *private = elem->head.mixer->private_data;

	ucontrol->value.enumerated.item[0] = private->mux[elem->control];
	return 0;
}

static int scarlett2_mux_src_enum_ctl_put(struct snd_kcontrol *kctl,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_mixer_data *private = mixer->private_data;
	int index = elem->control;
	int oval, val, err = 0;

	mutex_lock(&private->data_mutex);

	oval = private->mux[index];
	val = clamp(ucontrol->value.integer.value[0],
		    0L, private->num_mux_srcs - 1L);

	if (oval == val)
		goto unlock;

	private->mux[index] = val;
	err = scarlett2_usb_set_mux(mixer);
	if (err == 0)
		err = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_mux_src_enum_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = scarlett2_mux_src_enum_ctl_info,
	.get  = scarlett2_mux_src_enum_ctl_get,
	.put  = scarlett2_mux_src_enum_ctl_put,
};

static int scarlett2_add_mux_enums(struct usb_mixer_interface *mixer)
{
	struct scarlett2_mixer_data *private = mixer->private_data;
	const struct scarlett2_ports *ports = private->info->ports;
	int port_type, channel, i;

	for (i = 0, port_type = 0;
	     port_type < SCARLETT2_PORT_TYPE_COUNT;
	     port_type++) {
		for (channel = 0;
		     channel < ports[port_type].num[SCARLETT2_PORT_OUT];
		     channel++, i++) {
			int err;
			char s[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
			const char *const descr = ports[port_type].dst_descr;

			snprintf(s, sizeof(s) - 5, descr, channel + 1);
			strcat(s, " Enum");

			err = scarlett2_add_new_ctl(mixer,
						    &scarlett2_mux_src_enum_ctl,
						    i, 1, s, NULL);
			if (err < 0)
				return err;
		}
	}

	return 0;
}

/*** Meter Controls ***/

static int scarlett2_meter_ctl_info(struct snd_kcontrol *kctl,
				    struct snd_ctl_elem_info *uinfo)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = elem->channels;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 4095;
	uinfo->value.integer.step = 1;
	return 0;
}

static int scarlett2_meter_ctl_get(struct snd_kcontrol *kctl,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	u16 meter_levels[SCARLETT2_NUM_METERS];
	int i, err;

	err = scarlett2_usb_get_meter_levels(elem->head.mixer, meter_levels);
	if (err < 0)
		return err;

	for (i = 0; i < elem->channels; i++)
		ucontrol->value.integer.value[i] = meter_levels[i];

	return 0;
}

static const struct snd_kcontrol_new scarlett2_meter_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_PCM,
	.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.name = "",
	.info = scarlett2_meter_ctl_info,
	.get  = scarlett2_meter_ctl_get
};

static int scarlett2_add_meter_ctl(struct usb_mixer_interface *mixer)
{
	return scarlett2_add_new_ctl(mixer, &scarlett2_meter_ctl,
				     0, SCARLETT2_NUM_METERS,
				     "Level Meter", NULL);
}

/*** Cleanup/Suspend Callbacks ***/

static void scarlett2_private_free(struct usb_mixer_interface *mixer)
{
	struct scarlett2_mixer_data *private = mixer->private_data;

	cancel_delayed_work_sync(&private->work);
	kfree(private);
	mixer->private_data = NULL;
}

static void scarlett2_private_suspend(struct usb_mixer_interface *mixer)
{
	struct scarlett2_mixer_data *private = mixer->private_data;

	if (cancel_delayed_work_sync(&private->work))
		scarlett2_config_save(private->mixer);
}

/*** Initialisation ***/

static int scarlett2_count_mux_srcs(const struct scarlett2_ports *ports)
{
	int port_type, count = 0;

	for (port_type = 0;
	     port_type < SCARLETT2_PORT_TYPE_COUNT;
	     port_type++)
		count += ports[port_type].num[SCARLETT2_PORT_IN];

	return count;
}

/* Default routing connects PCM outputs and inputs to Analogue,
 * S/PDIF, then ADAT
 */
static void scarlett2_init_routing(u8 *mux,
				   const struct scarlett2_ports *ports)
{
	int i, input_num, input_count, port_type;
	int output_num, output_count, port_type_connect_num;

	static const int connect_order[] = {
		SCARLETT2_PORT_TYPE_ANALOGUE,
		SCARLETT2_PORT_TYPE_SPDIF,
		SCARLETT2_PORT_TYPE_ADAT,
		-1
	};

	/* Assign PCM inputs (routing outputs) */
	output_num = scarlett2_get_port_start_num(ports,
						  SCARLETT2_PORT_OUT,
						  SCARLETT2_PORT_TYPE_PCM);
	output_count = ports[SCARLETT2_PORT_TYPE_PCM].num[SCARLETT2_PORT_OUT];

	for (port_type = connect_order[port_type_connect_num = 0];
	     port_type >= 0;
	     port_type = connect_order[++port_type_connect_num]) {
		input_num = scarlett2_get_port_start_num(
			ports, SCARLETT2_PORT_IN, port_type);
		input_count = ports[port_type].num[SCARLETT2_PORT_IN];
		for (i = 0;
		     i < input_count && output_count;
		     i++, output_count--)
			mux[output_num++] = input_num++;
	}

	/* Assign PCM outputs (routing inputs) */
	input_num = scarlett2_get_port_start_num(ports,
						 SCARLETT2_PORT_IN,
						 SCARLETT2_PORT_TYPE_PCM);
	input_count = ports[SCARLETT2_PORT_TYPE_PCM].num[SCARLETT2_PORT_IN];

	for (port_type = connect_order[port_type_connect_num = 0];
	     port_type >= 0;
	     port_type = connect_order[++port_type_connect_num]) {
		output_num = scarlett2_get_port_start_num(
			ports, SCARLETT2_PORT_OUT, port_type);
		output_count = ports[port_type].num[SCARLETT2_PORT_OUT];
		for (i = 0;
		     i < output_count && input_count;
		     i++, input_count--)
			mux[output_num++] = input_num++;
	}
}

/* Initialise private data, routing, sequence number */
static int scarlett2_init_private(struct usb_mixer_interface *mixer,
				  const struct scarlett2_device_info *info)
{
	struct scarlett2_mixer_data *private =
		kzalloc(sizeof(struct scarlett2_mixer_data), GFP_KERNEL);

	if (!private)
		return -ENOMEM;

	mutex_init(&private->usb_mutex);
	mutex_init(&private->data_mutex);
	INIT_DELAYED_WORK(&private->work, scarlett2_config_save_work);
	private->info = info;
	private->num_mux_srcs = scarlett2_count_mux_srcs(info->ports);
	private->scarlett2_seq = 0;
	private->mixer = mixer;
	mixer->private_data = private;
	mixer->private_free = scarlett2_private_free;
	mixer->private_suspend = scarlett2_private_suspend;

	/* Setup default routing */
	scarlett2_init_routing(private->mux, info->ports);

	/* Initialise the sequence number used for the proprietary commands */
	return scarlett2_usb(mixer, SCARLETT2_USB_INIT_SEQ, NULL, 0, NULL, 0);
}

/* Read line-in config and line-out volume settings on start */
static int scarlett2_read_configs(struct usb_mixer_interface *mixer)
{
	struct scarlett2_mixer_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	const struct scarlett2_ports *ports = info->ports;
	int num_line_out =
		ports[SCARLETT2_PORT_TYPE_ANALOGUE].num[SCARLETT2_PORT_OUT];
	u8 level_switches[SCARLETT2_LEVEL_SWITCH_MAX];
	u8 pad_switches[SCARLETT2_PAD_SWITCH_MAX];
	struct scarlett2_usb_volume_status volume_status;
	int err, i;

	if (info->level_input_count) {
		err = scarlett2_usb_get_config(
			mixer,
			SCARLETT2_CONFIG_LEVEL_SWITCH,
			info->level_input_count,
			level_switches);
		if (err < 0)
			return err;
		for (i = 0; i < info->level_input_count; i++)
			private->level_switch[i] = level_switches[i];
	}

	if (info->pad_input_count) {
		err = scarlett2_usb_get_config(
			mixer,
			SCARLETT2_CONFIG_PAD_SWITCH,
			info->pad_input_count,
			pad_switches);
		if (err < 0)
			return err;
		for (i = 0; i < info->pad_input_count; i++)
			private->pad_switch[i] = pad_switches[i];
	}

	err = scarlett2_usb_get_volume_status(mixer, &volume_status);
	if (err < 0)
		return err;

	private->master_vol = clamp(
		volume_status.master_vol + SCARLETT2_VOLUME_BIAS,
		0, SCARLETT2_VOLUME_BIAS);

	for (i = 0; i < num_line_out; i++) {
		int volume;

		private->vol_sw_hw_switch[i] =
			info->line_out_hw_vol
				&& volume_status.sw_hw_switch[i];

		volume = private->vol_sw_hw_switch[i]
			   ? volume_status.master_vol
			   : volume_status.sw_vol[i];
		volume = clamp(volume + SCARLETT2_VOLUME_BIAS,
			       0, SCARLETT2_VOLUME_BIAS);
		private->vol[i] = volume;
	}

	for (i = 0; i < info->button_count; i++)
		private->buttons[i] = !!volume_status.buttons[i];

	return 0;
}

/* Notify on volume change */
static void scarlett2_mixer_interrupt_vol_change(
	struct usb_mixer_interface *mixer)
{
	struct scarlett2_mixer_data *private = mixer->private_data;
	const struct scarlett2_ports *ports = private->info->ports;
	int num_line_out =
		ports[SCARLETT2_PORT_TYPE_ANALOGUE].num[SCARLETT2_PORT_OUT];
	int i;

	private->vol_updated = 1;

	snd_ctl_notify(mixer->chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
		       &private->master_vol_ctl->id);

	for (i = 0; i < num_line_out; i++) {
		if (!private->vol_sw_hw_switch[i])
			continue;
		snd_ctl_notify(mixer->chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &private->vol_ctls[i]->id);
	}
}

/* Notify on button change */
static void scarlett2_mixer_interrupt_button_change(
	struct usb_mixer_interface *mixer)
{
	struct scarlett2_mixer_data *private = mixer->private_data;
	int i;

	private->vol_updated = 1;

	for (i = 0; i < private->info->button_count; i++)
		snd_ctl_notify(mixer->chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &private->button_ctls[i]->id);
}

/* Interrupt callback */
static void scarlett2_mixer_interrupt(struct urb *urb)
{
	struct usb_mixer_interface *mixer = urb->context;
	int len = urb->actual_length;
	int ustatus = urb->status;
	u32 data;

	if (ustatus != 0)
		goto requeue;

	if (len == 8) {
		data = le32_to_cpu(*(__le32 *)urb->transfer_buffer);
		if (data & SCARLETT2_USB_INTERRUPT_VOL_CHANGE)
			scarlett2_mixer_interrupt_vol_change(mixer);
		if (data & SCARLETT2_USB_INTERRUPT_BUTTON_CHANGE)
			scarlett2_mixer_interrupt_button_change(mixer);
	} else {
		usb_audio_err(mixer->chip,
			      "scarlett mixer interrupt length %d\n", len);
	}

requeue:
	if (ustatus != -ENOENT &&
	    ustatus != -ECONNRESET &&
	    ustatus != -ESHUTDOWN) {
		urb->dev = mixer->chip->dev;
		usb_submit_urb(urb, GFP_ATOMIC);
	}
}

static int scarlett2_mixer_status_create(struct usb_mixer_interface *mixer)
{
	struct usb_device *dev = mixer->chip->dev;
	unsigned int pipe = usb_rcvintpipe(dev,
					   SCARLETT2_USB_INTERRUPT_ENDPOINT);
	void *transfer_buffer;

	if (mixer->urb) {
		usb_audio_err(mixer->chip,
			      "%s: mixer urb already in use!\n", __func__);
		return 0;
	}

	if (usb_pipe_type_check(dev, pipe))
		return -EINVAL;

	mixer->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!mixer->urb)
		return -ENOMEM;

	transfer_buffer = kmalloc(SCARLETT2_USB_INTERRUPT_MAX_DATA, GFP_KERNEL);
	if (!transfer_buffer)
		return -ENOMEM;

	usb_fill_int_urb(mixer->urb, dev, pipe,
			 transfer_buffer, SCARLETT2_USB_INTERRUPT_MAX_DATA,
			 scarlett2_mixer_interrupt, mixer,
			 SCARLETT2_USB_INTERRUPT_INTERVAL);

	return usb_submit_urb(mixer->urb, GFP_KERNEL);
}

static int snd_scarlett_gen2_controls_create(struct usb_mixer_interface *mixer,
					     const struct scarlett2_device_info *info)
{
	int err;

	/* Initialise private data, routing, sequence number */
	err = scarlett2_init_private(mixer, info);
	if (err < 0)
		return err;

	/* Read volume levels and controls from the interface */
	err = scarlett2_read_configs(mixer);
	if (err < 0)
		return err;

	/* Create the analogue output controls */
	err = scarlett2_add_line_out_ctls(mixer);
	if (err < 0)
		return err;

	/* Create the analogue input controls */
	err = scarlett2_add_line_in_ctls(mixer);
	if (err < 0)
		return err;

	/* Create the input, output, and mixer mux input selections */
	err = scarlett2_add_mux_enums(mixer);
	if (err < 0)
		return err;

	/* Create the matrix mixer controls */
	err = scarlett2_add_mixer_ctls(mixer);
	if (err < 0)
		return err;

	/* Create the level meter controls */
	err = scarlett2_add_meter_ctl(mixer);
	if (err < 0)
		return err;

	/* Set up the interrupt polling if there are hardware buttons */
	if (info->button_count) {
		err = scarlett2_mixer_status_create(mixer);
		if (err < 0)
			return err;
	}

	return 0;
}

int snd_scarlett_gen2_init(struct usb_mixer_interface *mixer)
{
	struct snd_usb_audio *chip = mixer->chip;
	const struct scarlett2_device_info *info;
	int err;

	/* only use UAC_VERSION_2 */
	if (!mixer->protocol)
		return 0;

	switch (chip->usb_id) {
	case USB_ID(0x1235, 0x8203):
		info = &s6i6_gen2_info;
		break;
	case USB_ID(0x1235, 0x8204):
		info = &s18i8_gen2_info;
		break;
	case USB_ID(0x1235, 0x8201):
		info = &s18i20_gen2_info;
		break;
	default: /* device not (yet) supported */
		return -EINVAL;
	}

	if (!(chip->setup & SCARLETT2_ENABLE)) {
		usb_audio_info(chip,
			"Focusrite Scarlett Gen 2 Mixer Driver disabled; "
			"use options snd_usb_audio vid=0x%04x pid=0x%04x "
			"device_setup=1 to enable and report any issues "
			"to g@b4.vu",
			USB_ID_VENDOR(chip->usb_id),
			USB_ID_PRODUCT(chip->usb_id));
		return 0;
	}

	usb_audio_info(chip,
		"Focusrite Scarlett Gen 2 Mixer Driver enabled pid=0x%04x",
		USB_ID_PRODUCT(chip->usb_id));

	err = snd_scarlett_gen2_controls_create(mixer, info);
	if (err < 0)
		usb_audio_err(mixer->chip,
			      "Error initialising Scarlett Mixer Driver: %d",
			      err);

	return err;
}
