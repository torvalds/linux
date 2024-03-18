// SPDX-License-Identifier: GPL-2.0
/*
 *   Focusrite Scarlett 2 Protocol Driver for ALSA
 *   (including Scarlett 2nd Gen, 3rd Gen, 4th Gen, Clarett USB, and
 *   Clarett+ series products)
 *
 *   Supported models:
 *   - 6i6/18i8/18i20 Gen 2
 *   - Solo/2i2/4i4/8i6/18i8/18i20 Gen 3
 *   - Solo/2i2/4i4 Gen 4
 *   - Clarett 2Pre/4Pre/8Pre USB
 *   - Clarett+ 2Pre/4Pre/8Pre
 *
 *   Copyright (c) 2018-2023 by Geoffrey D. Bennett <g at b4.vu>
 *   Copyright (c) 2020-2021 by Vladimir Sadovnikov <sadko4u@gmail.com>
 *   Copyright (c) 2022 by Christian Colglazier <christian@cacolglazier.com>
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

/* The protocol was reverse engineered by looking at the communication
 * between Focusrite Control 2.3.4 and the Focusrite(R) Scarlett 18i20
 * (firmware 1083) using usbmon in July-August 2018.
 *
 * Scarlett 18i8 support added in April 2019.
 *
 * Scarlett 6i6 support added in June 2019 (thanks to Martin Wittmann
 * for providing usbmon output and testing).
 *
 * Scarlett 4i4/8i6 Gen 3 support added in May 2020 (thanks to Laurent
 * Debricon for donating a 4i4 and to Fredrik Unger for providing 8i6
 * usbmon output and testing).
 *
 * Scarlett 18i8/18i20 Gen 3 support added in June 2020 (thanks to
 * Darren Jaeckel, Alex Sedlack, and Clovis Lunel for providing usbmon
 * output, protocol traces and testing).
 *
 * Support for loading mixer volume and mux configuration from the
 * interface during driver initialisation added in May 2021 (thanks to
 * Vladimir Sadovnikov for figuring out how).
 *
 * Support for Solo/2i2 Gen 3 added in May 2021 (thanks to Alexander
 * Vorona for 2i2 protocol traces).
 *
 * Support for phantom power, direct monitoring, speaker switching,
 * and talkback added in May-June 2021.
 *
 * Support for Clarett+ 8Pre added in Aug 2022 by Christian
 * Colglazier.
 *
 * Support for Clarett 8Pre USB added in Sep 2023 (thanks to Philippe
 * Perrot for confirmation).
 *
 * Support for Clarett+ 4Pre and 2Pre added in Sep 2023 (thanks to
 * Gregory Rozzo for donating a 4Pre, and David Sherwood and Patrice
 * Peterson for usbmon output).
 *
 * Support for Clarett 2Pre and 4Pre USB added in Oct 2023.
 *
 * Support for firmware updates added in Dec 2023.
 *
 * Support for Scarlett Solo/2i2/4i4 Gen 4 added in Dec 2023 (thanks
 * to many LinuxMusicians people and to Focusrite for hardware
 * donations).
 *
 * This ALSA mixer gives access to (model-dependent):
 *  - input, output, mixer-matrix muxes
 *  - mixer-matrix gain stages
 *  - gain/volume/mute controls
 *  - level meters
 *  - line/inst level, pad, and air controls
 *  - phantom power, direct monitor, speaker switching, and talkback
 *    controls
 *  - disable/enable MSD mode
 *  - disable/enable standalone mode
 *  - input gain, autogain, safe mode
 *  - direct monitor mixes
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
 * Gen 3/4 devices have a Mass Storage Device (MSD) mode where a small
 * disk with registration and driver download information is presented
 * to the host. To access the full functionality of the device without
 * proprietary software, MSD mode can be disabled by:
 * - holding down the 48V button for five seconds while powering on
 *   the device, or
 * - using this driver and alsamixer to change the "MSD Mode" setting
 *   to Off and power-cycling the device
 */

#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/moduleparam.h>

#include <sound/control.h>
#include <sound/tlv.h>
#include <sound/hwdep.h>

#include <uapi/sound/scarlett2.h>

#include "usbaudio.h"
#include "mixer.h"
#include "helper.h"

#include "mixer_scarlett2.h"

/* device_setup value to allow turning MSD mode back on */
#define SCARLETT2_MSD_ENABLE 0x02

/* device_setup value to disable this mixer driver */
#define SCARLETT2_DISABLE 0x04

/* some gui mixers can't handle negative ctl values */
#define SCARLETT2_VOLUME_BIAS 127

/* maximum preamp input gain and value
 * values are from 0 to 70, preamp gain is from 0 to 69 dB
 */
#define SCARLETT2_MAX_GAIN_VALUE 70
#define SCARLETT2_MAX_GAIN_DB 69

/* mixer range from -80dB to +6dB in 0.5dB steps */
#define SCARLETT2_MIXER_MIN_DB -80
#define SCARLETT2_MIXER_BIAS (-SCARLETT2_MIXER_MIN_DB * 2)
#define SCARLETT2_MIXER_MAX_DB 6
#define SCARLETT2_MIXER_MAX_VALUE \
	((SCARLETT2_MIXER_MAX_DB - SCARLETT2_MIXER_MIN_DB) * 2)
#define SCARLETT2_MIXER_VALUE_COUNT (SCARLETT2_MIXER_MAX_VALUE + 1)

/* map from (dB + 80) * 2 to mixer value
 * for dB in 0 .. 172: int(8192 * pow(10, ((dB - 160) / 2 / 20)))
 */
static const u16 scarlett2_mixer_values[SCARLETT2_MIXER_VALUE_COUNT] = {
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

/* Maximum number of various input controls */
#define SCARLETT2_LEVEL_SWITCH_MAX 2
#define SCARLETT2_PAD_SWITCH_MAX 8
#define SCARLETT2_AIR_SWITCH_MAX 8
#define SCARLETT2_PHANTOM_SWITCH_MAX 2
#define SCARLETT2_INPUT_GAIN_MAX 2

/* Maximum number of inputs to the mixer */
#define SCARLETT2_INPUT_MIX_MAX 25

/* Maximum number of outputs from the mixer */
#define SCARLETT2_OUTPUT_MIX_MAX 12

/* Maximum number of mixer gain controls */
#define SCARLETT2_MIX_MAX (SCARLETT2_INPUT_MIX_MAX * SCARLETT2_OUTPUT_MIX_MAX)

/* Maximum number of direct monitor mixer gain controls
 * 1 (Solo) or 2 (2i2) direct monitor selections (Mono & Stereo)
 * 2 Mix outputs (A/Left & B/Right)
 * 4 Mix inputs
 */
#define SCARLETT2_MONITOR_MIX_MAX (2 * 2 * 4)

/* Maximum size of the data in the USB mux assignment message:
 * 20 inputs, 20 outputs, 25 matrix inputs, 12 spare
 */
#define SCARLETT2_MUX_MAX 77

/* Maximum number of sources (sum of input port counts) */
#define SCARLETT2_MAX_SRCS 52

/* Maximum number of meters (sum of output port counts) */
#define SCARLETT2_MAX_METERS 65

/* Hardware port types:
 * - None (no input to mux)
 * - Analogue I/O
 * - S/PDIF I/O
 * - ADAT I/O
 * - Mixer I/O
 * - PCM I/O
 */
enum {
	SCARLETT2_PORT_TYPE_NONE,
	SCARLETT2_PORT_TYPE_ANALOGUE,
	SCARLETT2_PORT_TYPE_SPDIF,
	SCARLETT2_PORT_TYPE_ADAT,
	SCARLETT2_PORT_TYPE_MIX,
	SCARLETT2_PORT_TYPE_PCM,
	SCARLETT2_PORT_TYPE_COUNT
};

/* I/O count of each port type kept in struct scarlett2_ports */
enum {
	SCARLETT2_PORT_IN,
	SCARLETT2_PORT_OUT,
	SCARLETT2_PORT_DIRNS
};

/* Dim/Mute buttons on the 18i20 */
enum {
	SCARLETT2_BUTTON_MUTE,
	SCARLETT2_BUTTON_DIM,
	SCARLETT2_DIM_MUTE_COUNT
};

/* Flash Write State */
enum {
	SCARLETT2_FLASH_WRITE_STATE_IDLE,
	SCARLETT2_FLASH_WRITE_STATE_SELECTED,
	SCARLETT2_FLASH_WRITE_STATE_ERASING,
	SCARLETT2_FLASH_WRITE_STATE_WRITE
};

static const char *const scarlett2_dim_mute_names[SCARLETT2_DIM_MUTE_COUNT] = {
	"Mute Playback Switch", "Dim Playback Switch"
};

/* The autogain_status is set based on the autogain_switch and
 * raw_autogain_status values.
 *
 * If autogain_switch is set, autogain_status is set to 0 (Running).
 * The other status values are from the raw_autogain_status value + 1.
 */
static const char *const scarlett2_autogain_status_texts[] = {
	"Running",
	"Success",
	"SuccessDRover",
	"WarnMinGainLimit",
	"FailDRunder",
	"FailMaxGainLimit",
	"FailClipped",
	"Cancelled",
	"Invalid"
};

/* Power Status Values */
enum {
	SCARLETT2_POWER_STATUS_EXT,
	SCARLETT2_POWER_STATUS_BUS,
	SCARLETT2_POWER_STATUS_FAIL,
	SCARLETT2_POWER_STATUS_COUNT
};

/* Notification callback functions */
struct scarlett2_notification {
	u32 mask;
	void (*func)(struct usb_mixer_interface *mixer);
};

static void scarlett2_notify_sync(struct usb_mixer_interface *mixer);
static void scarlett2_notify_dim_mute(struct usb_mixer_interface *mixer);
static void scarlett2_notify_monitor(struct usb_mixer_interface *mixer);
static void scarlett2_notify_volume(struct usb_mixer_interface *mixer);
static void scarlett2_notify_input_level(struct usb_mixer_interface *mixer);
static void scarlett2_notify_input_pad(struct usb_mixer_interface *mixer);
static void scarlett2_notify_input_air(struct usb_mixer_interface *mixer);
static void scarlett2_notify_input_phantom(struct usb_mixer_interface *mixer);
static void scarlett2_notify_input_other(struct usb_mixer_interface *mixer);
static void scarlett2_notify_input_select(struct usb_mixer_interface *mixer);
static void scarlett2_notify_input_gain(struct usb_mixer_interface *mixer);
static void scarlett2_notify_autogain(struct usb_mixer_interface *mixer);
static void scarlett2_notify_input_safe(struct usb_mixer_interface *mixer);
static void scarlett2_notify_monitor_other(struct usb_mixer_interface *mixer);
static void scarlett2_notify_direct_monitor(struct usb_mixer_interface *mixer);
static void scarlett2_notify_power_status(struct usb_mixer_interface *mixer);
static void scarlett2_notify_pcm_input_switch(
					struct usb_mixer_interface *mixer);

/* Arrays of notification callback functions */

static const struct scarlett2_notification scarlett2_notifications[] = {
	{ 0x00000001, NULL }, /* ack, gets ignored */
	{ 0x00000008, scarlett2_notify_sync },
	{ 0x00200000, scarlett2_notify_dim_mute },
	{ 0x00400000, scarlett2_notify_monitor },
	{ 0x00800000, scarlett2_notify_input_other },
	{ 0x01000000, scarlett2_notify_monitor_other },
	{ 0, NULL }
};

static const struct scarlett2_notification scarlett3a_notifications[] = {
	{ 0x00000001, NULL }, /* ack, gets ignored */
	{ 0x00800000, scarlett2_notify_input_other },
	{ 0x01000000, scarlett2_notify_direct_monitor },
	{ 0, NULL }
};

static const struct scarlett2_notification scarlett4_solo_notifications[] = {
	{ 0x00000001, NULL }, /* ack, gets ignored */
	{ 0x00000008, scarlett2_notify_sync },
	{ 0x00400000, scarlett2_notify_input_air },
	{ 0x00800000, scarlett2_notify_direct_monitor },
	{ 0x01000000, scarlett2_notify_input_level },
	{ 0x02000000, scarlett2_notify_input_phantom },
	{ 0x04000000, scarlett2_notify_pcm_input_switch },
	{ 0, NULL }
};

static const struct scarlett2_notification scarlett4_2i2_notifications[] = {
	{ 0x00000001, NULL }, /* ack, gets ignored */
	{ 0x00000008, scarlett2_notify_sync },
	{ 0x00200000, scarlett2_notify_input_safe },
	{ 0x00400000, scarlett2_notify_autogain },
	{ 0x00800000, scarlett2_notify_input_air },
	{ 0x01000000, scarlett2_notify_direct_monitor },
	{ 0x02000000, scarlett2_notify_input_select },
	{ 0x04000000, scarlett2_notify_input_level },
	{ 0x08000000, scarlett2_notify_input_phantom },
	{ 0x10000000, NULL }, /* power status, ignored */
	{ 0x40000000, scarlett2_notify_input_gain },
	{ 0x80000000, NULL }, /* power status, ignored */
	{ 0, NULL }
};

static const struct scarlett2_notification scarlett4_4i4_notifications[] = {
	{ 0x00000001, NULL }, /* ack, gets ignored */
	{ 0x00000008, scarlett2_notify_sync },
	{ 0x00200000, scarlett2_notify_input_safe },
	{ 0x00400000, scarlett2_notify_autogain },
	{ 0x00800000, scarlett2_notify_input_air },
	{ 0x01000000, scarlett2_notify_input_select },
	{ 0x02000000, scarlett2_notify_input_level },
	{ 0x04000000, scarlett2_notify_input_phantom },
	{ 0x08000000, scarlett2_notify_power_status }, /* power external */
	{ 0x20000000, scarlett2_notify_input_gain },
	{ 0x40000000, scarlett2_notify_power_status }, /* power status */
	{ 0x80000000, scarlett2_notify_volume },
	{ 0, NULL }
};

/* Configuration parameters that can be read and written */
enum {
	SCARLETT2_CONFIG_DIM_MUTE,
	SCARLETT2_CONFIG_LINE_OUT_VOLUME,
	SCARLETT2_CONFIG_MUTE_SWITCH,
	SCARLETT2_CONFIG_SW_HW_SWITCH,
	SCARLETT2_CONFIG_MASTER_VOLUME,
	SCARLETT2_CONFIG_HEADPHONE_VOLUME,
	SCARLETT2_CONFIG_LEVEL_SWITCH,
	SCARLETT2_CONFIG_PAD_SWITCH,
	SCARLETT2_CONFIG_MSD_SWITCH,
	SCARLETT2_CONFIG_AIR_SWITCH,
	SCARLETT2_CONFIG_STANDALONE_SWITCH,
	SCARLETT2_CONFIG_PHANTOM_SWITCH,
	SCARLETT2_CONFIG_PHANTOM_PERSISTENCE,
	SCARLETT2_CONFIG_DIRECT_MONITOR,
	SCARLETT2_CONFIG_MONITOR_OTHER_SWITCH,
	SCARLETT2_CONFIG_MONITOR_OTHER_ENABLE,
	SCARLETT2_CONFIG_TALKBACK_MAP,
	SCARLETT2_CONFIG_AUTOGAIN_SWITCH,
	SCARLETT2_CONFIG_AUTOGAIN_STATUS,
	SCARLETT2_CONFIG_INPUT_GAIN,
	SCARLETT2_CONFIG_SAFE_SWITCH,
	SCARLETT2_CONFIG_INPUT_SELECT_SWITCH,
	SCARLETT2_CONFIG_INPUT_LINK_SWITCH,
	SCARLETT2_CONFIG_POWER_EXT,
	SCARLETT2_CONFIG_POWER_LOW,
	SCARLETT2_CONFIG_PCM_INPUT_SWITCH,
	SCARLETT2_CONFIG_DIRECT_MONITOR_GAIN,
	SCARLETT2_CONFIG_COUNT
};

/* Location, size, and activation command number for the configuration
 * parameters. Size is in bits and may be 0, 1, 8, or 16.
 *
 * A size of 0 indicates that the parameter is a byte-sized Scarlett
 * Gen 4 configuration which is written through the gen4_write_addr
 * location (but still read through the given offset location).
 *
 * Some Gen 4 configuration parameters are written with 0x02 for a
 * desired value of 0x01, and 0x03 for 0x00. These are indicated with
 * mute set to 1. 0x02 and 0x03 are temporary values while the device
 * makes the change and the channel and/or corresponding DSP channel
 * output is muted.
 */
struct scarlett2_config {
	u16 offset;
	u8 size;
	u8 activate;
	u8 mute;
};

struct scarlett2_config_set {
	const struct scarlett2_notification *notifications;
	u16 gen4_write_addr;
	const struct scarlett2_config items[SCARLETT2_CONFIG_COUNT];
};

/* Gen 2 devices without SW/HW volume switch: 6i6, 18i8 */

static const struct scarlett2_config_set scarlett2_config_set_gen2a = {
	.notifications = scarlett2_notifications,
	.items = {
		[SCARLETT2_CONFIG_LINE_OUT_VOLUME] = {
			.offset = 0x34, .size = 16, .activate = 1 },

		[SCARLETT2_CONFIG_MUTE_SWITCH] = {
			.offset = 0x5c, .size = 8, .activate = 1 },

		[SCARLETT2_CONFIG_LEVEL_SWITCH] = {
			.offset = 0x7c, .size = 8, .activate = 7 },

		[SCARLETT2_CONFIG_PAD_SWITCH] = {
			.offset = 0x84, .size = 8, .activate = 8 },

		[SCARLETT2_CONFIG_STANDALONE_SWITCH] = {
			.offset = 0x8d, .size = 8, .activate = 6 },
	}
};

/* Gen 2 devices with SW/HW volume switch: 18i20 */

static const struct scarlett2_config_set scarlett2_config_set_gen2b = {
	.notifications = scarlett2_notifications,
	.items = {
		[SCARLETT2_CONFIG_DIM_MUTE] = {
			.offset = 0x31, .size = 8, .activate = 2 },

		[SCARLETT2_CONFIG_LINE_OUT_VOLUME] = {
			.offset = 0x34, .size = 16, .activate = 1 },

		[SCARLETT2_CONFIG_MUTE_SWITCH] = {
			.offset = 0x5c, .size = 8, .activate = 1 },

		[SCARLETT2_CONFIG_SW_HW_SWITCH] = {
			.offset = 0x66, .size = 8, .activate = 3 },

		[SCARLETT2_CONFIG_MASTER_VOLUME] = {
			.offset = 0x76, .size = 16 },

		[SCARLETT2_CONFIG_LEVEL_SWITCH] = {
			.offset = 0x7c, .size = 8, .activate = 7 },

		[SCARLETT2_CONFIG_PAD_SWITCH] = {
			.offset = 0x84, .size = 8, .activate = 8 },

		[SCARLETT2_CONFIG_STANDALONE_SWITCH] = {
			.offset = 0x8d, .size = 8, .activate = 6 },
	}
};

/* Gen 3 devices without a mixer (Solo and 2i2) */
static const struct scarlett2_config_set scarlett2_config_set_gen3a = {
	.notifications = scarlett3a_notifications,
	.items = {
		[SCARLETT2_CONFIG_MSD_SWITCH] = {
			.offset = 0x04, .size = 8, .activate = 6 },

		[SCARLETT2_CONFIG_PHANTOM_PERSISTENCE] = {
			.offset = 0x05, .size = 8, .activate = 6 },

		[SCARLETT2_CONFIG_PHANTOM_SWITCH] = {
			.offset = 0x06, .size = 8, .activate = 3 },

		[SCARLETT2_CONFIG_DIRECT_MONITOR] = {
			.offset = 0x07, .size = 8, .activate = 4 },

		[SCARLETT2_CONFIG_LEVEL_SWITCH] = {
			.offset = 0x08, .size = 1, .activate = 7 },

		[SCARLETT2_CONFIG_AIR_SWITCH] = {
			.offset = 0x09, .size = 1, .activate = 8 },
	}
};

/* Gen 3 devices without SW/HW volume switch: 4i4, 8i6 */
static const struct scarlett2_config_set scarlett2_config_set_gen3b = {
	.notifications = scarlett2_notifications,
	.items = {
		[SCARLETT2_CONFIG_LINE_OUT_VOLUME] = {
			.offset = 0x34, .size = 16, .activate = 1 },

		[SCARLETT2_CONFIG_MUTE_SWITCH] = {
			.offset = 0x5c, .size = 8, .activate = 1 },

		[SCARLETT2_CONFIG_LEVEL_SWITCH] = {
			.offset = 0x7c, .size = 8, .activate = 7 },

		[SCARLETT2_CONFIG_PAD_SWITCH] = {
			.offset = 0x84, .size = 8, .activate = 8 },

		[SCARLETT2_CONFIG_AIR_SWITCH] = {
			.offset = 0x8c, .size = 8, .activate = 8 },

		[SCARLETT2_CONFIG_STANDALONE_SWITCH] = {
			.offset = 0x95, .size = 8, .activate = 6 },

		[SCARLETT2_CONFIG_PHANTOM_SWITCH] = {
			.offset = 0x9c, .size = 1, .activate = 8 },

		[SCARLETT2_CONFIG_MSD_SWITCH] = {
			.offset = 0x9d, .size = 8, .activate = 6 },

		[SCARLETT2_CONFIG_PHANTOM_PERSISTENCE] = {
			.offset = 0x9e, .size = 8, .activate = 6 },
	}
};

/* Gen 3 devices with SW/HW volume switch: 18i8, 18i20 */
static const struct scarlett2_config_set scarlett2_config_set_gen3c = {
	.notifications = scarlett2_notifications,
	.items = {
		[SCARLETT2_CONFIG_DIM_MUTE] = {
			.offset = 0x31, .size = 8, .activate = 2 },

		[SCARLETT2_CONFIG_LINE_OUT_VOLUME] = {
			.offset = 0x34, .size = 16, .activate = 1 },

		[SCARLETT2_CONFIG_MUTE_SWITCH] = {
			.offset = 0x5c, .size = 8, .activate = 1 },

		[SCARLETT2_CONFIG_SW_HW_SWITCH] = {
			.offset = 0x66, .size = 8, .activate = 3 },

		[SCARLETT2_CONFIG_MASTER_VOLUME] = {
			.offset = 0x76, .size = 16 },

		[SCARLETT2_CONFIG_LEVEL_SWITCH] = {
			.offset = 0x7c, .size = 8, .activate = 7 },

		[SCARLETT2_CONFIG_PAD_SWITCH] = {
			.offset = 0x84, .size = 8, .activate = 8 },

		[SCARLETT2_CONFIG_AIR_SWITCH] = {
			.offset = 0x8c, .size = 8, .activate = 8 },

		[SCARLETT2_CONFIG_STANDALONE_SWITCH] = {
			.offset = 0x95, .size = 8, .activate = 6 },

		[SCARLETT2_CONFIG_PHANTOM_SWITCH] = {
			.offset = 0x9c, .size = 1, .activate = 8 },

		[SCARLETT2_CONFIG_MSD_SWITCH] = {
			.offset = 0x9d, .size = 8, .activate = 6 },

		[SCARLETT2_CONFIG_PHANTOM_PERSISTENCE] = {
			.offset = 0x9e, .size = 8, .activate = 6 },

		[SCARLETT2_CONFIG_MONITOR_OTHER_SWITCH] = {
			.offset = 0x9f, .size = 1, .activate = 10 },

		[SCARLETT2_CONFIG_MONITOR_OTHER_ENABLE] = {
			.offset = 0xa0, .size = 1, .activate = 10 },

		[SCARLETT2_CONFIG_TALKBACK_MAP] = {
			.offset = 0xb0, .size = 16, .activate = 10 },
	}
};

/* Solo Gen 4 */
static const struct scarlett2_config_set scarlett2_config_set_gen4_solo = {
	.notifications = scarlett4_solo_notifications,
	.gen4_write_addr = 0xd8,
	.items = {
		[SCARLETT2_CONFIG_MSD_SWITCH] = {
			.offset = 0x47, .size = 8, .activate = 4 },

		[SCARLETT2_CONFIG_DIRECT_MONITOR] = {
			.offset = 0x108, .activate = 12 },

		[SCARLETT2_CONFIG_PHANTOM_SWITCH] = {
			.offset = 0x46, .activate = 9, .mute = 1 },

		[SCARLETT2_CONFIG_LEVEL_SWITCH] = {
			.offset = 0x3d, .activate = 10, .mute = 1 },

		[SCARLETT2_CONFIG_AIR_SWITCH] = {
			.offset = 0x3e, .activate = 11 },

		[SCARLETT2_CONFIG_PCM_INPUT_SWITCH] = {
			.offset = 0x206, .activate = 25 },

		[SCARLETT2_CONFIG_DIRECT_MONITOR_GAIN] = {
			.offset = 0x232, .size = 16, .activate = 26 }
	}
};

/* 2i2 Gen 4 */
static const struct scarlett2_config_set scarlett2_config_set_gen4_2i2 = {
	.notifications = scarlett4_2i2_notifications,
	.gen4_write_addr = 0xfc,
	.items = {
		[SCARLETT2_CONFIG_MSD_SWITCH] = {
			.offset = 0x49, .size = 8, .activate = 4 }, // 0x41 ??

		[SCARLETT2_CONFIG_DIRECT_MONITOR] = {
			.offset = 0x14a, .activate = 16 },

		[SCARLETT2_CONFIG_AUTOGAIN_SWITCH] = {
			.offset = 0x135, .activate = 10 },

		[SCARLETT2_CONFIG_AUTOGAIN_STATUS] = {
			.offset = 0x137 },

		[SCARLETT2_CONFIG_PHANTOM_SWITCH] = {
			.offset = 0x48, .activate = 11, .mute = 1 },

		[SCARLETT2_CONFIG_INPUT_GAIN] = {
			.offset = 0x4b, .activate = 12 },

		[SCARLETT2_CONFIG_LEVEL_SWITCH] = {
			.offset = 0x3c, .activate = 13, .mute = 1 },

		[SCARLETT2_CONFIG_SAFE_SWITCH] = {
			.offset = 0x147, .activate = 14 },

		[SCARLETT2_CONFIG_AIR_SWITCH] = {
			.offset = 0x3e, .activate = 15 },

		[SCARLETT2_CONFIG_INPUT_SELECT_SWITCH] = {
			.offset = 0x14b, .activate = 17 },

		[SCARLETT2_CONFIG_INPUT_LINK_SWITCH] = {
			.offset = 0x14e, .activate = 18 },

		[SCARLETT2_CONFIG_DIRECT_MONITOR_GAIN] = {
			.offset = 0x2a0, .size = 16, .activate = 36 }
	}
};

/* 4i4 Gen 4 */
static const struct scarlett2_config_set scarlett2_config_set_gen4_4i4 = {
	.notifications = scarlett4_4i4_notifications,
	.gen4_write_addr = 0x130,
	.items = {
		[SCARLETT2_CONFIG_MSD_SWITCH] = {
			.offset = 0x5c, .size = 8, .activate = 4 },

		[SCARLETT2_CONFIG_AUTOGAIN_SWITCH] = {
			.offset = 0x13e, .activate = 10 },

		[SCARLETT2_CONFIG_AUTOGAIN_STATUS] = {
			.offset = 0x140 },

		[SCARLETT2_CONFIG_PHANTOM_SWITCH] = {
			.offset = 0x5a, .activate = 11, .mute = 1 },

		[SCARLETT2_CONFIG_INPUT_GAIN] = {
			.offset = 0x5e, .activate = 12 },

		[SCARLETT2_CONFIG_LEVEL_SWITCH] = {
			.offset = 0x4e, .activate = 13, .mute = 1 },

		[SCARLETT2_CONFIG_SAFE_SWITCH] = {
			.offset = 0x150, .activate = 14 },

		[SCARLETT2_CONFIG_AIR_SWITCH] = {
			.offset = 0x50, .activate = 15 },

		[SCARLETT2_CONFIG_INPUT_SELECT_SWITCH] = {
			.offset = 0x153, .activate = 16 },

		[SCARLETT2_CONFIG_INPUT_LINK_SWITCH] = {
			.offset = 0x156, .activate = 17 },

		[SCARLETT2_CONFIG_MASTER_VOLUME] = {
			.offset = 0x32, .size = 16 },

		[SCARLETT2_CONFIG_HEADPHONE_VOLUME] = {
			.offset = 0x3a, .size = 16 },

		[SCARLETT2_CONFIG_POWER_EXT] = {
			.offset = 0x168 },

		[SCARLETT2_CONFIG_POWER_LOW] = {
			.offset = 0x16d }
	}
};

/* Clarett USB and Clarett+ devices: 2Pre, 4Pre, 8Pre */
static const struct scarlett2_config_set scarlett2_config_set_clarett = {
	.notifications = scarlett2_notifications,
	.items = {
		[SCARLETT2_CONFIG_DIM_MUTE] = {
			.offset = 0x31, .size = 8, .activate = 2 },

		[SCARLETT2_CONFIG_LINE_OUT_VOLUME] = {
			.offset = 0x34, .size = 16, .activate = 1 },

		[SCARLETT2_CONFIG_MUTE_SWITCH] = {
			.offset = 0x5c, .size = 8, .activate = 1 },

		[SCARLETT2_CONFIG_SW_HW_SWITCH] = {
			.offset = 0x66, .size = 8, .activate = 3 },

		[SCARLETT2_CONFIG_MASTER_VOLUME] = {
			.offset = 0x76, .size = 16 },

		[SCARLETT2_CONFIG_LEVEL_SWITCH] = {
			.offset = 0x7c, .size = 8, .activate = 7 },

		[SCARLETT2_CONFIG_AIR_SWITCH] = {
			.offset = 0x95, .size = 8, .activate = 8 },

		[SCARLETT2_CONFIG_STANDALONE_SWITCH] = {
			.offset = 0x8d, .size = 8, .activate = 6 },
	}
};

/* Description of each hardware port type:
 * - id: hardware ID of this port type
 * - src_descr: printf format string for mux input selections
 * - src_num_offset: added to channel number for the fprintf
 * - dst_descr: printf format string for mixer controls
 */
struct scarlett2_port {
	u16 id;
	const char * const src_descr;
	int src_num_offset;
	const char * const dst_descr;
	const char * const dsp_src_descr;
	const char * const dsp_dst_descr;
};

static const struct scarlett2_port scarlett2_ports[SCARLETT2_PORT_TYPE_COUNT] = {
	[SCARLETT2_PORT_TYPE_NONE] = {
		.id = 0x000,
		.src_descr = "Off"
	},
	[SCARLETT2_PORT_TYPE_ANALOGUE] = {
		.id = 0x080,
		.src_descr = "Analogue %d",
		.src_num_offset = 1,
		.dst_descr = "Analogue Output %02d Playback"
	},
	[SCARLETT2_PORT_TYPE_SPDIF] = {
		.id = 0x180,
		.src_descr = "S/PDIF %d",
		.src_num_offset = 1,
		.dst_descr = "S/PDIF Output %d Playback"
	},
	[SCARLETT2_PORT_TYPE_ADAT] = {
		.id = 0x200,
		.src_descr = "ADAT %d",
		.src_num_offset = 1,
		.dst_descr = "ADAT Output %d Playback"
	},
	[SCARLETT2_PORT_TYPE_MIX] = {
		.id = 0x300,
		.src_descr = "Mix %c",
		.src_num_offset = 'A',
		.dst_descr = "Mixer Input %02d Capture",
		.dsp_src_descr = "DSP %d",
		.dsp_dst_descr = "DSP Input %d Capture"
	},
	[SCARLETT2_PORT_TYPE_PCM] = {
		.id = 0x600,
		.src_descr = "PCM %d",
		.src_num_offset = 1,
		.dst_descr = "PCM %02d Capture"
	},
};

/* Number of mux tables: one for each band of sample rates
 * (44.1/48kHz, 88.2/96kHz, and 176.4/176kHz)
 */
#define SCARLETT2_MUX_TABLES 3

/* Maximum number of entries in a mux table */
#define SCARLETT2_MAX_MUX_ENTRIES 10

/* One entry within mux_assignment defines the port type and range of
 * ports to add to the set_mux message. The end of the list is marked
 * with count == 0.
 */
struct scarlett2_mux_entry {
	u8 port_type;
	u8 start;
	u8 count;
};

/* Maximum number of entries in a mux table */
#define SCARLETT2_MAX_METER_ENTRIES 9

/* One entry within meter_assignment defines the range of mux outputs
 * that consecutive meter entries are mapped to. The end of the list
 * is marked with count == 0.
 */
struct scarlett2_meter_entry {
	u8 start;
	u8 count;
};

struct scarlett2_device_info {
	/* which set of configuration parameters the device uses */
	const struct scarlett2_config_set *config_set;

	/* minimum firmware version required */
	u16 min_firmware_version;

	/* support for main/alt speaker switching */
	u8 has_speaker_switching;

	/* support for talkback microphone */
	u8 has_talkback;

	/* the number of analogue inputs with a software switchable
	 * level control that can be set to line or instrument
	 */
	u8 level_input_count;

	/* the first input with a level control (0-based) */
	u8 level_input_first;

	/* the number of analogue inputs with a software switchable
	 * 10dB pad control
	 */
	u8 pad_input_count;

	/* the number of analogue inputs with a software switchable
	 * "air" control
	 */
	u8 air_input_count;

	/* the first input with an air control (0-based) */
	u8 air_input_first;

	/* number of additional air options
	 * 0 for air presence only (Gen 3)
	 * 1 for air presence+drive (Gen 4)
	 */
	u8 air_option;

	/* the number of phantom (48V) software switchable controls */
	u8 phantom_count;

	/* the first input with phantom power control (0-based) */
	u8 phantom_first;

	/* the number of inputs each phantom switch controls */
	u8 inputs_per_phantom;

	/* the number of inputs with software-controllable gain */
	u8 gain_input_count;

	/* the number of direct monitor options
	 * (0 = none, 1 = mono only, 2 = mono/stereo)
	 */
	u8 direct_monitor;

	/* the number of DSP channels */
	u8 dsp_count;

	/* remap analogue outputs; 18i8 Gen 3 has "line 3/4" connected
	 * internally to the analogue 7/8 outputs
	 */
	u8 line_out_remap_enable;
	u8 line_out_remap[SCARLETT2_ANALOGUE_MAX];
	u8 line_out_unmap[SCARLETT2_ANALOGUE_MAX];

	/* additional description for the line out volume controls */
	const char * const line_out_descrs[SCARLETT2_ANALOGUE_MAX];

	/* number of sources/destinations of each port type */
	const int port_count[SCARLETT2_PORT_TYPE_COUNT][SCARLETT2_PORT_DIRNS];

	/* layout/order of the entries in the set_mux message */
	struct scarlett2_mux_entry mux_assignment[SCARLETT2_MUX_TABLES]
						 [SCARLETT2_MAX_MUX_ENTRIES];

	/* map from meter level order returned by
	 * SCARLETT2_USB_GET_METER to index into mux[] entries (same
	 * as the order returned by scarlett2_meter_ctl_get())
	 */
	struct scarlett2_meter_entry meter_map[SCARLETT2_MAX_METER_ENTRIES];
};

struct scarlett2_data {
	struct usb_mixer_interface *mixer;
	struct mutex usb_mutex; /* prevent sending concurrent USB requests */
	struct mutex data_mutex; /* lock access to this data */
	u8 hwdep_in_use;
	u8 selected_flash_segment_id;
	u8 flash_write_state;
	struct delayed_work work;
	const struct scarlett2_device_info *info;
	const struct scarlett2_config_set *config_set;
	const char *series_name;
	__u8 bInterfaceNumber;
	__u8 bEndpointAddress;
	__u16 wMaxPacketSize;
	__u8 bInterval;
	u8 num_mux_srcs;
	u8 num_mux_dsts;
	u8 num_mix_in;
	u8 num_mix_out;
	u8 num_line_out;
	u8 num_monitor_mix_ctls;
	u32 firmware_version;
	u8 flash_segment_nums[SCARLETT2_SEGMENT_ID_COUNT];
	u8 flash_segment_blocks[SCARLETT2_SEGMENT_ID_COUNT];
	u16 scarlett2_seq;
	u8 sync_updated;
	u8 vol_updated;
	u8 dim_mute_updated;
	u8 input_level_updated;
	u8 input_pad_updated;
	u8 input_air_updated;
	u8 input_phantom_updated;
	u8 input_select_updated;
	u8 input_gain_updated;
	u8 autogain_updated;
	u8 input_safe_updated;
	u8 pcm_input_switch_updated;
	u8 monitor_other_updated;
	u8 direct_monitor_updated;
	u8 mux_updated;
	u8 mix_updated;
	u8 speaker_switching_switched;
	u8 power_status_updated;
	u8 sync;
	u8 master_vol;
	u8 headphone_vol;
	u8 vol[SCARLETT2_ANALOGUE_MAX];
	u8 vol_sw_hw_switch[SCARLETT2_ANALOGUE_MAX];
	u8 mute_switch[SCARLETT2_ANALOGUE_MAX];
	u8 level_switch[SCARLETT2_LEVEL_SWITCH_MAX];
	u8 pad_switch[SCARLETT2_PAD_SWITCH_MAX];
	u8 dim_mute[SCARLETT2_DIM_MUTE_COUNT];
	u8 air_switch[SCARLETT2_AIR_SWITCH_MAX];
	u8 phantom_switch[SCARLETT2_PHANTOM_SWITCH_MAX];
	u8 phantom_persistence;
	u8 input_select_switch;
	u8 input_link_switch[SCARLETT2_INPUT_GAIN_MAX / 2];
	u8 gain[SCARLETT2_INPUT_GAIN_MAX];
	u8 autogain_switch[SCARLETT2_INPUT_GAIN_MAX];
	u8 autogain_status[SCARLETT2_INPUT_GAIN_MAX];
	u8 safe_switch[SCARLETT2_INPUT_GAIN_MAX];
	u8 pcm_input_switch;
	u8 direct_monitor_switch;
	u8 speaker_switching_switch;
	u8 talkback_switch;
	u8 talkback_map[SCARLETT2_OUTPUT_MIX_MAX];
	u8 msd_switch;
	u8 standalone_switch;
	u8 power_status;
	u8 meter_level_map[SCARLETT2_MAX_METERS];
	struct snd_kcontrol *sync_ctl;
	struct snd_kcontrol *master_vol_ctl;
	struct snd_kcontrol *headphone_vol_ctl;
	struct snd_kcontrol *vol_ctls[SCARLETT2_ANALOGUE_MAX];
	struct snd_kcontrol *sw_hw_ctls[SCARLETT2_ANALOGUE_MAX];
	struct snd_kcontrol *mute_ctls[SCARLETT2_ANALOGUE_MAX];
	struct snd_kcontrol *dim_mute_ctls[SCARLETT2_DIM_MUTE_COUNT];
	struct snd_kcontrol *level_ctls[SCARLETT2_LEVEL_SWITCH_MAX];
	struct snd_kcontrol *pad_ctls[SCARLETT2_PAD_SWITCH_MAX];
	struct snd_kcontrol *air_ctls[SCARLETT2_AIR_SWITCH_MAX];
	struct snd_kcontrol *phantom_ctls[SCARLETT2_PHANTOM_SWITCH_MAX];
	struct snd_kcontrol *input_select_ctl;
	struct snd_kcontrol *input_link_ctls[SCARLETT2_INPUT_GAIN_MAX / 2];
	struct snd_kcontrol *input_gain_ctls[SCARLETT2_INPUT_GAIN_MAX];
	struct snd_kcontrol *autogain_ctls[SCARLETT2_INPUT_GAIN_MAX];
	struct snd_kcontrol *autogain_status_ctls[SCARLETT2_INPUT_GAIN_MAX];
	struct snd_kcontrol *safe_ctls[SCARLETT2_INPUT_GAIN_MAX];
	struct snd_kcontrol *pcm_input_switch_ctl;
	struct snd_kcontrol *mux_ctls[SCARLETT2_MUX_MAX];
	struct snd_kcontrol *mix_ctls[SCARLETT2_MIX_MAX];
	struct snd_kcontrol *direct_monitor_ctl;
	struct snd_kcontrol *speaker_switching_ctl;
	struct snd_kcontrol *talkback_ctl;
	struct snd_kcontrol *power_status_ctl;
	u8 mux[SCARLETT2_MUX_MAX];
	u8 mix[SCARLETT2_MIX_MAX];
	u8 monitor_mix[SCARLETT2_MONITOR_MIX_MAX];
};

/*** Model-specific data ***/

static const struct scarlett2_device_info s6i6_gen2_info = {
	.config_set = &scarlett2_config_set_gen2a,
	.level_input_count = 2,
	.pad_input_count = 2,

	.line_out_descrs = {
		"Headphones 1 L",
		"Headphones 1 R",
		"Headphones 2 L",
		"Headphones 2 R",
	},

	.port_count = {
		[SCARLETT2_PORT_TYPE_NONE]     = {  1,  0 },
		[SCARLETT2_PORT_TYPE_ANALOGUE] = {  4,  4 },
		[SCARLETT2_PORT_TYPE_SPDIF]    = {  2,  2 },
		[SCARLETT2_PORT_TYPE_MIX]      = { 10, 18 },
		[SCARLETT2_PORT_TYPE_PCM]      = {  6,  6 },
	},

	.mux_assignment = { {
		{ SCARLETT2_PORT_TYPE_PCM,      0,  6 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0,  4 },
		{ SCARLETT2_PORT_TYPE_SPDIF,    0,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,      0, 18 },
		{ SCARLETT2_PORT_TYPE_NONE,     0,  8 },
		{ 0,                            0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,      0,  6 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0,  4 },
		{ SCARLETT2_PORT_TYPE_SPDIF,    0,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,      0, 18 },
		{ SCARLETT2_PORT_TYPE_NONE,     0,  8 },
		{ 0,                            0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,      0,  6 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0,  4 },
		{ SCARLETT2_PORT_TYPE_SPDIF,    0,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,      0, 18 },
		{ SCARLETT2_PORT_TYPE_NONE,     0,  8 },
		{ 0,                            0,  0 },
	} },

	.meter_map = {
		{ 24,  6 },
		{  0, 24 },
		{  0,  0 },
	}
};

static const struct scarlett2_device_info s18i8_gen2_info = {
	.config_set = &scarlett2_config_set_gen2a,
	.level_input_count = 2,
	.pad_input_count = 4,

	.line_out_descrs = {
		"Monitor L",
		"Monitor R",
		"Headphones 1 L",
		"Headphones 1 R",
		"Headphones 2 L",
		"Headphones 2 R",
	},

	.port_count = {
		[SCARLETT2_PORT_TYPE_NONE]     = {  1,  0 },
		[SCARLETT2_PORT_TYPE_ANALOGUE] = {  8,  6 },
		[SCARLETT2_PORT_TYPE_SPDIF]    = {  2,  2 },
		[SCARLETT2_PORT_TYPE_ADAT]     = {  8,  0 },
		[SCARLETT2_PORT_TYPE_MIX]      = { 10, 18 },
		[SCARLETT2_PORT_TYPE_PCM]      = {  8, 18 },
	},

	.mux_assignment = { {
		{ SCARLETT2_PORT_TYPE_PCM,      0, 18 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0,  6 },
		{ SCARLETT2_PORT_TYPE_SPDIF,    0,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,      0, 18 },
		{ SCARLETT2_PORT_TYPE_NONE,     0,  8 },
		{ 0,                            0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,      0, 14 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0,  6 },
		{ SCARLETT2_PORT_TYPE_SPDIF,    0,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,      0, 18 },
		{ SCARLETT2_PORT_TYPE_NONE,     0,  8 },
		{ 0,                            0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,      0, 10 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0,  6 },
		{ SCARLETT2_PORT_TYPE_SPDIF,    0,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,      0, 18 },
		{ SCARLETT2_PORT_TYPE_NONE,     0,  4 },
		{ 0,                            0,  0 },
	} },

	.meter_map = {
		{ 26, 18 },
		{  0, 26 },
		{  0,  0 },
	}
};

static const struct scarlett2_device_info s18i20_gen2_info = {
	.config_set = &scarlett2_config_set_gen2b,

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

	.port_count = {
		[SCARLETT2_PORT_TYPE_NONE]     = {  1,  0 },
		[SCARLETT2_PORT_TYPE_ANALOGUE] = {  8, 10 },
		[SCARLETT2_PORT_TYPE_SPDIF]    = {  2,  2 },
		[SCARLETT2_PORT_TYPE_ADAT]     = {  8,  8 },
		[SCARLETT2_PORT_TYPE_MIX]      = { 10, 18 },
		[SCARLETT2_PORT_TYPE_PCM]      = { 20, 18 },
	},

	.mux_assignment = { {
		{ SCARLETT2_PORT_TYPE_PCM,      0, 18 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0, 10 },
		{ SCARLETT2_PORT_TYPE_SPDIF,    0,  2 },
		{ SCARLETT2_PORT_TYPE_ADAT,     0,  8 },
		{ SCARLETT2_PORT_TYPE_MIX,      0, 18 },
		{ SCARLETT2_PORT_TYPE_NONE,     0,  8 },
		{ 0,                            0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,      0, 14 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0, 10 },
		{ SCARLETT2_PORT_TYPE_SPDIF,    0,  2 },
		{ SCARLETT2_PORT_TYPE_ADAT,     0,  4 },
		{ SCARLETT2_PORT_TYPE_MIX,      0, 18 },
		{ SCARLETT2_PORT_TYPE_NONE,     0,  8 },
		{ 0,                            0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,      0, 10 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0, 10 },
		{ SCARLETT2_PORT_TYPE_SPDIF,    0,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,      0, 18 },
		{ SCARLETT2_PORT_TYPE_NONE,     0,  6 },
		{ 0,                            0,  0 },
	} },

	.meter_map = {
		{ 38, 18 },
		{  0, 38 },
		{  0,  0 },
	}
};

static const struct scarlett2_device_info solo_gen3_info = {
	.config_set = &scarlett2_config_set_gen3a,
	.level_input_count = 1,
	.level_input_first = 1,
	.air_input_count = 1,
	.phantom_count = 1,
	.inputs_per_phantom = 1,
	.direct_monitor = 1,
};

static const struct scarlett2_device_info s2i2_gen3_info = {
	.config_set = &scarlett2_config_set_gen3a,
	.level_input_count = 2,
	.air_input_count = 2,
	.phantom_count = 1,
	.inputs_per_phantom = 2,
	.direct_monitor = 2,
};

static const struct scarlett2_device_info s4i4_gen3_info = {
	.config_set = &scarlett2_config_set_gen3b,
	.level_input_count = 2,
	.pad_input_count = 2,
	.air_input_count = 2,
	.phantom_count = 1,
	.inputs_per_phantom = 2,

	.line_out_descrs = {
		"Monitor L",
		"Monitor R",
		"Headphones L",
		"Headphones R",
	},

	.port_count = {
		[SCARLETT2_PORT_TYPE_NONE]     = { 1, 0 },
		[SCARLETT2_PORT_TYPE_ANALOGUE] = { 4, 4 },
		[SCARLETT2_PORT_TYPE_MIX]      = { 6, 8 },
		[SCARLETT2_PORT_TYPE_PCM]      = { 4, 6 },
	},

	.mux_assignment = { {
		{ SCARLETT2_PORT_TYPE_PCM,      0,  6 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0,  4 },
		{ SCARLETT2_PORT_TYPE_MIX,      0,  8 },
		{ SCARLETT2_PORT_TYPE_NONE,     0, 16 },
		{ 0,                            0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,      0,  6 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0,  4 },
		{ SCARLETT2_PORT_TYPE_MIX,      0,  8 },
		{ SCARLETT2_PORT_TYPE_NONE,     0, 16 },
		{ 0,                            0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,      0,  6 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0,  4 },
		{ SCARLETT2_PORT_TYPE_MIX,      0,  8 },
		{ SCARLETT2_PORT_TYPE_NONE,     0, 16 },
		{ 0,                            0,  0 },
	} },

	.meter_map = {
		{ 12,  6 },
		{  0, 12 },
		{  0,  0 },
	}
};

static const struct scarlett2_device_info s8i6_gen3_info = {
	.config_set = &scarlett2_config_set_gen3b,
	.level_input_count = 2,
	.pad_input_count = 2,
	.air_input_count = 2,
	.phantom_count = 1,
	.inputs_per_phantom = 2,

	.line_out_descrs = {
		"Headphones 1 L",
		"Headphones 1 R",
		"Headphones 2 L",
		"Headphones 2 R",
	},

	.port_count = {
		[SCARLETT2_PORT_TYPE_NONE]     = { 1,  0 },
		[SCARLETT2_PORT_TYPE_ANALOGUE] = { 6,  4 },
		[SCARLETT2_PORT_TYPE_SPDIF]    = { 2,  2 },
		[SCARLETT2_PORT_TYPE_MIX]      = { 8,  8 },
		[SCARLETT2_PORT_TYPE_PCM]      = { 6, 10 },
	},

	.mux_assignment = { {
		{ SCARLETT2_PORT_TYPE_PCM,      0,  8 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0,  4 },
		{ SCARLETT2_PORT_TYPE_SPDIF,    0,  2 },
		{ SCARLETT2_PORT_TYPE_PCM,      8,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,      0,  8 },
		{ SCARLETT2_PORT_TYPE_NONE,     0, 18 },
		{ 0,                            0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,      0,  8 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0,  4 },
		{ SCARLETT2_PORT_TYPE_SPDIF,    0,  2 },
		{ SCARLETT2_PORT_TYPE_PCM,      8,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,      0,  8 },
		{ SCARLETT2_PORT_TYPE_NONE,     0, 18 },
		{ 0,                            0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,      0,  8 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0,  4 },
		{ SCARLETT2_PORT_TYPE_SPDIF,    0,  2 },
		{ SCARLETT2_PORT_TYPE_PCM,      8,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,      0,  8 },
		{ SCARLETT2_PORT_TYPE_NONE,     0, 18 },
		{ 0,                            0,  0 },
	} },

	.meter_map = {
		{ 14, 8 },
		{  0, 6 },
		{ 22, 2 },
		{  6, 8 },
		{  0, 0 },
	}
};

static const struct scarlett2_device_info s18i8_gen3_info = {
	.config_set = &scarlett2_config_set_gen3c,
	.has_speaker_switching = 1,
	.level_input_count = 2,
	.pad_input_count = 4,
	.air_input_count = 4,
	.phantom_count = 2,
	.inputs_per_phantom = 2,

	.line_out_remap_enable = 1,
	.line_out_remap = { 0, 1, 6, 7, 2, 3, 4, 5 },
	.line_out_unmap = { 0, 1, 4, 5, 6, 7, 2, 3 },

	.line_out_descrs = {
		"Monitor L",
		"Monitor R",
		"Alt Monitor L",
		"Alt Monitor R",
		"Headphones 1 L",
		"Headphones 1 R",
		"Headphones 2 L",
		"Headphones 2 R",
	},

	.port_count = {
		[SCARLETT2_PORT_TYPE_NONE]     = {  1,  0 },
		[SCARLETT2_PORT_TYPE_ANALOGUE] = {  8,  8 },
		[SCARLETT2_PORT_TYPE_SPDIF]    = {  2,  2 },
		[SCARLETT2_PORT_TYPE_ADAT]     = {  8,  0 },
		[SCARLETT2_PORT_TYPE_MIX]      = { 10, 20 },
		[SCARLETT2_PORT_TYPE_PCM]      = {  8, 20 },
	},

	.mux_assignment = { {
		{ SCARLETT2_PORT_TYPE_PCM,       0, 10 },
		{ SCARLETT2_PORT_TYPE_PCM,      12,  8 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  0,  2 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  6,  2 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  2,  4 },
		{ SCARLETT2_PORT_TYPE_SPDIF,     0,  2 },
		{ SCARLETT2_PORT_TYPE_PCM,      10,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,       0, 20 },
		{ SCARLETT2_PORT_TYPE_NONE,      0, 10 },
		{ 0,                             0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,       0, 10 },
		{ SCARLETT2_PORT_TYPE_PCM,      12,  4 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  0,  2 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  6,  2 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  2,  4 },
		{ SCARLETT2_PORT_TYPE_SPDIF,     0,  2 },
		{ SCARLETT2_PORT_TYPE_PCM,      10,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,       0, 20 },
		{ SCARLETT2_PORT_TYPE_NONE,      0, 10 },
		{ 0,                             0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,       0, 10 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  0,  2 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  6,  2 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  2,  4 },
		{ SCARLETT2_PORT_TYPE_SPDIF,     0,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,       0, 20 },
		{ SCARLETT2_PORT_TYPE_NONE,      0, 10 },
		{ 0,                             0,  0 },
	} },

	.meter_map = {
		{ 30, 10 },
		{ 42,  8 },
		{  0,  2 },
		{  6,  2 },
		{  2,  4 },
		{  8,  2 },
		{ 40,  2 },
		{ 10, 20 },
		{  0,  0 }
	}
};

static const struct scarlett2_device_info s18i20_gen3_info = {
	.config_set = &scarlett2_config_set_gen3c,
	.has_speaker_switching = 1,
	.has_talkback = 1,
	.level_input_count = 2,
	.pad_input_count = 8,
	.air_input_count = 8,
	.phantom_count = 2,
	.inputs_per_phantom = 4,

	.line_out_descrs = {
		"Monitor 1 L",
		"Monitor 1 R",
		"Monitor 2 L",
		"Monitor 2 R",
		NULL,
		NULL,
		"Headphones 1 L",
		"Headphones 1 R",
		"Headphones 2 L",
		"Headphones 2 R",
	},

	.port_count = {
		[SCARLETT2_PORT_TYPE_NONE]     = {  1,  0 },
		[SCARLETT2_PORT_TYPE_ANALOGUE] = {  9, 10 },
		[SCARLETT2_PORT_TYPE_SPDIF]    = {  2,  2 },
		[SCARLETT2_PORT_TYPE_ADAT]     = {  8,  8 },
		[SCARLETT2_PORT_TYPE_MIX]      = { 12, 25 },
		[SCARLETT2_PORT_TYPE_PCM]      = { 20, 20 },
	},

	.mux_assignment = { {
		{ SCARLETT2_PORT_TYPE_PCM,       0,  8 },
		{ SCARLETT2_PORT_TYPE_PCM,      10, 10 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  0, 10 },
		{ SCARLETT2_PORT_TYPE_SPDIF,     0,  2 },
		{ SCARLETT2_PORT_TYPE_ADAT,      0,  8 },
		{ SCARLETT2_PORT_TYPE_PCM,       8,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,       0, 25 },
		{ SCARLETT2_PORT_TYPE_NONE,      0, 12 },
		{ 0,                             0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,       0,  8 },
		{ SCARLETT2_PORT_TYPE_PCM,      10,  8 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  0, 10 },
		{ SCARLETT2_PORT_TYPE_SPDIF,     0,  2 },
		{ SCARLETT2_PORT_TYPE_ADAT,      0,  8 },
		{ SCARLETT2_PORT_TYPE_PCM,       8,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,       0, 25 },
		{ SCARLETT2_PORT_TYPE_NONE,      0, 10 },
		{ 0,                             0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,       0, 10 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  0, 10 },
		{ SCARLETT2_PORT_TYPE_SPDIF,     0,  2 },
		{ SCARLETT2_PORT_TYPE_NONE,      0, 24 },
		{ 0,                             0,  0 },
	} },

	.meter_map = {
		{ 45,  8 },
		{ 55, 10 },
		{  0, 20 },
		{ 53,  2 },
		{ 20, 25 },
		{  0,  0 },
	}
};

static const struct scarlett2_device_info solo_gen4_info = {
	.config_set = &scarlett2_config_set_gen4_solo,
	.min_firmware_version = 2115,

	.level_input_count = 1,
	.air_input_count = 1,
	.air_input_first = 1,
	.air_option = 1,
	.phantom_count = 1,
	.phantom_first = 1,
	.inputs_per_phantom = 1,
	.direct_monitor = 1,
	.dsp_count = 2,

	.port_count = {
		[SCARLETT2_PORT_TYPE_NONE]     = { 1,  0 },
		[SCARLETT2_PORT_TYPE_ANALOGUE] = { 2,  2 },
		[SCARLETT2_PORT_TYPE_MIX]      = { 8,  6 },
		[SCARLETT2_PORT_TYPE_PCM]      = { 2,  4 },
	},

	.mux_assignment = { {
		{ SCARLETT2_PORT_TYPE_MIX,       4,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,       2,  2 },
		{ SCARLETT2_PORT_TYPE_PCM,       0,  4 },
		{ SCARLETT2_PORT_TYPE_MIX,       0,  2 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  0,  2 },
		{ 0,                             0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_MIX,       4,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,       2,  2 },
		{ SCARLETT2_PORT_TYPE_PCM,       0,  4 },
		{ SCARLETT2_PORT_TYPE_MIX,       0,  2 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  0,  2 },
		{ 0,                             0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_MIX,       4,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,       2,  2 },
		{ SCARLETT2_PORT_TYPE_PCM,       0,  4 },
		{ SCARLETT2_PORT_TYPE_MIX,       0,  2 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  0,  2 },
		{ 0,                             0,  0 },
	} },

	.meter_map = {
		{  6,  2 },
		{  4,  2 },
		{  8,  4 },
		{  2,  2 },
		{  0,  2 },
		{  0,  0 }
	}
};

static const struct scarlett2_device_info s2i2_gen4_info = {
	.config_set = &scarlett2_config_set_gen4_2i2,
	.min_firmware_version = 2115,

	.level_input_count = 2,
	.air_input_count = 2,
	.air_option = 1,
	.phantom_count = 1,
	.inputs_per_phantom = 2,
	.gain_input_count = 2,
	.direct_monitor = 2,
	.dsp_count = 2,

	.port_count = {
		[SCARLETT2_PORT_TYPE_NONE]     = { 1,  0 },
		[SCARLETT2_PORT_TYPE_ANALOGUE] = { 2,  2 },
		[SCARLETT2_PORT_TYPE_MIX]      = { 6,  6 },
		[SCARLETT2_PORT_TYPE_PCM]      = { 2,  4 },
	},

	.mux_assignment = { {
		{ SCARLETT2_PORT_TYPE_MIX,       4,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,       2,  2 },
		{ SCARLETT2_PORT_TYPE_PCM,       0,  4 },
		{ SCARLETT2_PORT_TYPE_MIX,       0,  2 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  0,  2 },
		{ 0,                             0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_MIX,       4,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,       2,  2 },
		{ SCARLETT2_PORT_TYPE_PCM,       0,  4 },
		{ SCARLETT2_PORT_TYPE_MIX,       0,  2 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  0,  2 },
		{ 0,                             0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_MIX,       4,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,       2,  2 },
		{ SCARLETT2_PORT_TYPE_PCM,       0,  4 },
		{ SCARLETT2_PORT_TYPE_MIX,       0,  2 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  0,  2 },
		{ 0,                             0,  0 },
	} },

	.meter_map = {
		{  6,  2 },
		{  4,  2 },
		{  8,  4 },
		{  2,  2 },
		{  0,  2 },
		{  0,  0 }
	}
};

static const struct scarlett2_device_info s4i4_gen4_info = {
	.config_set = &scarlett2_config_set_gen4_4i4,
	.min_firmware_version = 2089,

	.level_input_count = 2,
	.air_input_count = 2,
	.air_option = 1,
	.phantom_count = 2,
	.inputs_per_phantom = 1,
	.gain_input_count = 2,
	.dsp_count = 2,

	.port_count = {
		[SCARLETT2_PORT_TYPE_NONE]     = { 1,  0 },
		[SCARLETT2_PORT_TYPE_ANALOGUE] = { 4,  6 },
		[SCARLETT2_PORT_TYPE_MIX]      = { 8, 12 },
		[SCARLETT2_PORT_TYPE_PCM]      = { 6,  6 },
	},

	.mux_assignment = { {
		{ SCARLETT2_PORT_TYPE_MIX,      10,  2 },
		{ SCARLETT2_PORT_TYPE_PCM,       0,  6 },
		{ SCARLETT2_PORT_TYPE_MIX,       0, 10 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  0,  6 },
		{ 0,                             0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_MIX,      10,  2 },
		{ SCARLETT2_PORT_TYPE_PCM,       0,  6 },
		{ SCARLETT2_PORT_TYPE_MIX,       0, 10 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  0,  6 },
		{ 0,                             0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_MIX,      10,  2 },
		{ SCARLETT2_PORT_TYPE_PCM,       0,  6 },
		{ SCARLETT2_PORT_TYPE_MIX,       0, 10 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE,  0,  6 },
		{ 0,                             0,  0 },
	} },

	.meter_map = {
		{ 16,  8 },
		{  6, 10 },
		{  0,  6 },
		{  0,  0 }
	}
};

static const struct scarlett2_device_info clarett_2pre_info = {
	.config_set = &scarlett2_config_set_clarett,
	.level_input_count = 2,
	.air_input_count = 2,

	.line_out_descrs = {
		"Monitor L",
		"Monitor R",
		"Headphones L",
		"Headphones R",
	},

	.port_count = {
		[SCARLETT2_PORT_TYPE_NONE]     = {  1,  0 },
		[SCARLETT2_PORT_TYPE_ANALOGUE] = {  2,  4 },
		[SCARLETT2_PORT_TYPE_SPDIF]    = {  2,  0 },
		[SCARLETT2_PORT_TYPE_ADAT]     = {  8,  0 },
		[SCARLETT2_PORT_TYPE_MIX]      = { 10, 18 },
		[SCARLETT2_PORT_TYPE_PCM]      = {  4, 12 },
	},

	.mux_assignment = { {
		{ SCARLETT2_PORT_TYPE_PCM,      0, 12 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0,  4 },
		{ SCARLETT2_PORT_TYPE_MIX,      0, 18 },
		{ SCARLETT2_PORT_TYPE_NONE,     0,  8 },
		{ 0,                            0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,      0,  8 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0,  4 },
		{ SCARLETT2_PORT_TYPE_MIX,      0, 18 },
		{ SCARLETT2_PORT_TYPE_NONE,     0,  8 },
		{ 0,                            0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,      0,  2 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0,  4 },
		{ SCARLETT2_PORT_TYPE_NONE,     0, 26 },
		{ 0,                            0,  0 },
	} },

	.meter_map = {
		{ 22, 12 },
		{  0, 22 },
		{  0,  0 }
	}
};

static const struct scarlett2_device_info clarett_4pre_info = {
	.config_set = &scarlett2_config_set_clarett,
	.level_input_count = 2,
	.air_input_count = 4,

	.line_out_descrs = {
		"Monitor L",
		"Monitor R",
		"Headphones 1 L",
		"Headphones 1 R",
		"Headphones 2 L",
		"Headphones 2 R",
	},

	.port_count = {
		[SCARLETT2_PORT_TYPE_NONE]     = {  1,  0 },
		[SCARLETT2_PORT_TYPE_ANALOGUE] = {  8,  6 },
		[SCARLETT2_PORT_TYPE_SPDIF]    = {  2,  2 },
		[SCARLETT2_PORT_TYPE_ADAT]     = {  8,  0 },
		[SCARLETT2_PORT_TYPE_MIX]      = { 10, 18 },
		[SCARLETT2_PORT_TYPE_PCM]      = {  8, 18 },
	},

	.mux_assignment = { {
		{ SCARLETT2_PORT_TYPE_PCM,      0, 18 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0,  6 },
		{ SCARLETT2_PORT_TYPE_SPDIF,    0,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,      0, 18 },
		{ SCARLETT2_PORT_TYPE_NONE,     0,  8 },
		{ 0,                            0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,      0, 14 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0,  6 },
		{ SCARLETT2_PORT_TYPE_SPDIF,    0,  2 },
		{ SCARLETT2_PORT_TYPE_MIX,      0, 18 },
		{ SCARLETT2_PORT_TYPE_NONE,     0,  8 },
		{ 0,                            0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,      0, 12 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0,  6 },
		{ SCARLETT2_PORT_TYPE_SPDIF,    0,  2 },
		{ SCARLETT2_PORT_TYPE_NONE,     0, 24 },
		{ 0,                            0,  0 },
	} },

	.meter_map = {
		{ 26, 18 },
		{  0, 26 },
		{  0,  0 }
	}
};

static const struct scarlett2_device_info clarett_8pre_info = {
	.config_set = &scarlett2_config_set_clarett,
	.level_input_count = 2,
	.air_input_count = 8,

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

	.port_count = {
		[SCARLETT2_PORT_TYPE_NONE]     = {  1,  0 },
		[SCARLETT2_PORT_TYPE_ANALOGUE] = {  8, 10 },
		[SCARLETT2_PORT_TYPE_SPDIF]    = {  2,  2 },
		[SCARLETT2_PORT_TYPE_ADAT]     = {  8,  8 },
		[SCARLETT2_PORT_TYPE_MIX]      = { 10, 18 },
		[SCARLETT2_PORT_TYPE_PCM]      = { 20, 18 },
	},

	.mux_assignment = { {
		{ SCARLETT2_PORT_TYPE_PCM,      0, 18 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0, 10 },
		{ SCARLETT2_PORT_TYPE_SPDIF,    0,  2 },
		{ SCARLETT2_PORT_TYPE_ADAT,     0,  8 },
		{ SCARLETT2_PORT_TYPE_MIX,      0, 18 },
		{ SCARLETT2_PORT_TYPE_NONE,     0,  8 },
		{ 0,                            0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,      0, 14 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0, 10 },
		{ SCARLETT2_PORT_TYPE_SPDIF,    0,  2 },
		{ SCARLETT2_PORT_TYPE_ADAT,     0,  4 },
		{ SCARLETT2_PORT_TYPE_MIX,      0, 18 },
		{ SCARLETT2_PORT_TYPE_NONE,     0,  8 },
		{ 0,                            0,  0 },
	}, {
		{ SCARLETT2_PORT_TYPE_PCM,      0, 12 },
		{ SCARLETT2_PORT_TYPE_ANALOGUE, 0, 10 },
		{ SCARLETT2_PORT_TYPE_SPDIF,    0,  2 },
		{ SCARLETT2_PORT_TYPE_NONE,     0, 22 },
		{ 0,                            0,  0 },
	} },

	.meter_map = {
		{ 38, 18 },
		{  0, 38 },
		{  0,  0 }
	}
};

struct scarlett2_device_entry {
	const u32 usb_id; /* USB device identifier */
	const struct scarlett2_device_info *info;
	const char *series_name;
};

static const struct scarlett2_device_entry scarlett2_devices[] = {
	/* Supported Gen 2 devices */
	{ USB_ID(0x1235, 0x8203), &s6i6_gen2_info, "Scarlett Gen 2" },
	{ USB_ID(0x1235, 0x8204), &s18i8_gen2_info, "Scarlett Gen 2" },
	{ USB_ID(0x1235, 0x8201), &s18i20_gen2_info, "Scarlett Gen 2" },

	/* Supported Gen 3 devices */
	{ USB_ID(0x1235, 0x8211), &solo_gen3_info, "Scarlett Gen 3" },
	{ USB_ID(0x1235, 0x8210), &s2i2_gen3_info, "Scarlett Gen 3" },
	{ USB_ID(0x1235, 0x8212), &s4i4_gen3_info, "Scarlett Gen 3" },
	{ USB_ID(0x1235, 0x8213), &s8i6_gen3_info, "Scarlett Gen 3" },
	{ USB_ID(0x1235, 0x8214), &s18i8_gen3_info, "Scarlett Gen 3" },
	{ USB_ID(0x1235, 0x8215), &s18i20_gen3_info, "Scarlett Gen 3" },

	/* Supported Gen 4 devices */
	{ USB_ID(0x1235, 0x8218), &solo_gen4_info, "Scarlett Gen 4" },
	{ USB_ID(0x1235, 0x8219), &s2i2_gen4_info, "Scarlett Gen 4" },
	{ USB_ID(0x1235, 0x821a), &s4i4_gen4_info, "Scarlett Gen 4" },

	/* Supported Clarett USB/Clarett+ devices */
	{ USB_ID(0x1235, 0x8206), &clarett_2pre_info, "Clarett USB" },
	{ USB_ID(0x1235, 0x8207), &clarett_4pre_info, "Clarett USB" },
	{ USB_ID(0x1235, 0x8208), &clarett_8pre_info, "Clarett USB" },
	{ USB_ID(0x1235, 0x820a), &clarett_2pre_info, "Clarett+" },
	{ USB_ID(0x1235, 0x820b), &clarett_4pre_info, "Clarett+" },
	{ USB_ID(0x1235, 0x820c), &clarett_8pre_info, "Clarett+" },

	/* End of list */
	{ 0, NULL },
};

/* get the starting port index number for a given port type/direction */
static int scarlett2_get_port_start_num(
	const int port_count[][SCARLETT2_PORT_DIRNS],
	int direction, int port_type)
{
	int i, num = 0;

	for (i = 0; i < port_type; i++)
		num += port_count[i][direction];

	return num;
}

/*** USB Interactions ***/

/* Commands for sending/receiving requests/responses */
#define SCARLETT2_USB_CMD_INIT 0
#define SCARLETT2_USB_CMD_REQ  2
#define SCARLETT2_USB_CMD_RESP 3

#define SCARLETT2_USB_INIT_1        0x00000000
#define SCARLETT2_USB_INIT_2        0x00000002
#define SCARLETT2_USB_REBOOT        0x00000003
#define SCARLETT2_USB_GET_METER     0x00001001
#define SCARLETT2_USB_GET_MIX       0x00002001
#define SCARLETT2_USB_SET_MIX       0x00002002
#define SCARLETT2_USB_GET_MUX       0x00003001
#define SCARLETT2_USB_SET_MUX       0x00003002
#define SCARLETT2_USB_INFO_FLASH    0x00004000
#define SCARLETT2_USB_INFO_SEGMENT  0x00004001
#define SCARLETT2_USB_ERASE_SEGMENT 0x00004002
#define SCARLETT2_USB_GET_ERASE     0x00004003
#define SCARLETT2_USB_WRITE_SEGMENT 0x00004004
#define SCARLETT2_USB_GET_SYNC      0x00006004
#define SCARLETT2_USB_GET_DATA      0x00800000
#define SCARLETT2_USB_SET_DATA      0x00800001
#define SCARLETT2_USB_DATA_CMD      0x00800002

#define SCARLETT2_USB_CONFIG_SAVE 6

#define SCARLETT2_USB_METER_LEVELS_GET_MAGIC 1

#define SCARLETT2_FLASH_BLOCK_SIZE 4096
#define SCARLETT2_FLASH_WRITE_MAX 1024
#define SCARLETT2_SEGMENT_NUM_MIN 1
#define SCARLETT2_SEGMENT_NUM_MAX 4

#define SCARLETT2_SEGMENT_SETTINGS_NAME "App_Settings"
#define SCARLETT2_SEGMENT_FIRMWARE_NAME "App_Upgrade"

/* proprietary request/response format */
struct scarlett2_usb_packet {
	__le32 cmd;
	__le16 size;
	__le16 seq;
	__le32 error;
	__le32 pad;
	u8 data[];
};

static void scarlett2_fill_request_header(struct scarlett2_data *private,
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

static int scarlett2_usb_tx(struct usb_device *dev, int interface,
			    void *buf, u16 size)
{
	return snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0),
			SCARLETT2_USB_CMD_REQ,
			USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
			0, interface, buf, size);
}

static int scarlett2_usb_rx(struct usb_device *dev, int interface,
			    u32 usb_req, void *buf, u16 size)
{
	return snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0),
			usb_req,
			USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
			0, interface, buf, size);
}

/* Send a proprietary format request to the Scarlett interface */
static int scarlett2_usb(
	struct usb_mixer_interface *mixer, u32 cmd,
	void *req_data, u16 req_size, void *resp_data, u16 resp_size)
{
	struct scarlett2_data *private = mixer->private_data;
	struct usb_device *dev = mixer->chip->dev;
	struct scarlett2_usb_packet *req, *resp = NULL;
	size_t req_buf_size = struct_size(req, data, req_size);
	size_t resp_buf_size = struct_size(resp, data, resp_size);
	int err;

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

	err = scarlett2_usb_tx(dev, private->bInterfaceNumber,
			       req, req_buf_size);

	if (err != req_buf_size) {
		usb_audio_err(
			mixer->chip,
			"%s USB request result cmd %x was %d\n",
			private->series_name, cmd, err);
		err = -EINVAL;
		goto unlock;
	}

	/* send a second message to get the response */

	err = scarlett2_usb_rx(dev, private->bInterfaceNumber,
			       SCARLETT2_USB_CMD_RESP,
			       resp, resp_buf_size);

	/* validate the response */

	if (err != resp_buf_size) {

		/* ESHUTDOWN and EPROTO are valid responses to a
		 * reboot request
		 */
		if (cmd == SCARLETT2_USB_REBOOT &&
		    (err == -ESHUTDOWN || err == -EPROTO)) {
			err = 0;
			goto unlock;
		}

		usb_audio_err(
			mixer->chip,
			"%s USB response result cmd %x was %d expected %zu\n",
			private->series_name, cmd, err, resp_buf_size);
		err = -EINVAL;
		goto unlock;
	}

	/* cmd/seq/size should match except when initialising
	 * seq sent = 1, response = 0
	 */
	if (resp->cmd != req->cmd ||
	    (resp->seq != req->seq &&
		(le16_to_cpu(req->seq) != 1 || resp->seq != 0)) ||
	    resp_size != le16_to_cpu(resp->size) ||
	    resp->error ||
	    resp->pad) {
		usb_audio_err(
			mixer->chip,
			"%s USB invalid response; "
			   "cmd tx/rx %d/%d seq %d/%d size %d/%d "
			   "error %d pad %d\n",
			private->series_name,
			le32_to_cpu(req->cmd), le32_to_cpu(resp->cmd),
			le16_to_cpu(req->seq), le16_to_cpu(resp->seq),
			resp_size, le16_to_cpu(resp->size),
			le32_to_cpu(resp->error),
			le32_to_cpu(resp->pad));
		err = -EINVAL;
		goto unlock;
	}

	if (resp_data && resp_size > 0)
		memcpy(resp_data, resp->data, resp_size);

unlock:
	mutex_unlock(&private->usb_mutex);
error:
	kfree(req);
	kfree(resp);
	return err;
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

/* Return true if the given configuration item is present in the
 * configuration set used by this device.
 */
static int scarlett2_has_config_item(
	struct scarlett2_data *private, int config_item_num)
{
	return !!private->config_set->items[config_item_num].offset;
}

/* Send a USB message to get configuration parameters; result placed in *buf */
static int scarlett2_usb_get_config(
	struct usb_mixer_interface *mixer,
	int config_item_num, int count, void *buf)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_config *config_item =
		&private->config_set->items[config_item_num];
	int size, err, i;
	u8 *buf_8;
	u8 value;

	/* Check that the configuration item is present in the
	 * configuration set used by this device
	 */
	if (!config_item->offset)
		return -EFAULT;

	/* Gen 4 style parameters are always 1 byte */
	size = config_item->size ? config_item->size : 8;

	/* For byte-sized parameters, retrieve directly into buf */
	if (size >= 8) {
		size = size / 8 * count;
		err = scarlett2_usb_get(mixer, config_item->offset, buf, size);
		if (err < 0)
			return err;
		if (size == 2) {
			u16 *buf_16 = buf;

			for (i = 0; i < count; i++, buf_16++)
				*buf_16 = le16_to_cpu(*(__le16 *)buf_16);
		}
		return 0;
	}

	/* For bit-sized parameters, retrieve into value */
	err = scarlett2_usb_get(mixer, config_item->offset, &value, 1);
	if (err < 0)
		return err;

	/* then unpack from value into buf[] */
	buf_8 = buf;
	for (i = 0; i < 8 && i < count; i++, value >>= 1)
		*buf_8++ = value & 1;

	return 0;
}

/* Send a SCARLETT2_USB_SET_DATA command.
 * offset: location in the device's data space
 * size: size in bytes of the value (1, 2, 4)
 */
static int scarlett2_usb_set_data(
	struct usb_mixer_interface *mixer,
	int offset, int size, int value)
{
	struct scarlett2_data *private = mixer->private_data;
	struct {
		__le32 offset;
		__le32 size;
		__le32 value;
	} __packed req;

	req.offset = cpu_to_le32(offset);
	req.size = cpu_to_le32(size);
	req.value = cpu_to_le32(value);
	return scarlett2_usb(private->mixer, SCARLETT2_USB_SET_DATA,
			     &req, sizeof(u32) * 2 + size, NULL, 0);
}

/* Send a SCARLETT2_USB_DATA_CMD command.
 * Configuration changes require activation with this after they have
 * been uploaded by a previous SCARLETT2_USB_SET_DATA.
 * The value for activate needed is determined by the configuration
 * item.
 */
static int scarlett2_usb_activate_config(
	struct usb_mixer_interface *mixer, int activate)
{
	__le32 req;

	req = cpu_to_le32(activate);
	return scarlett2_usb(mixer, SCARLETT2_USB_DATA_CMD,
			     &req, sizeof(req), NULL, 0);
}

/* Send USB messages to set a SCARLETT2_CONFIG_* parameter */
static int scarlett2_usb_set_config(
	struct usb_mixer_interface *mixer,
	int config_item_num, int index, int value)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_config_set *config_set = private->config_set;
	const struct scarlett2_config *config_item =
		&config_set->items[config_item_num];
	int offset, size;
	int err;

	/* Check that the configuration item is present in the
	 * configuration set used by this device
	 */
	if (!config_item->offset)
		return -EFAULT;

	/* Gen 4 style writes are selected with size = 0;
	 * these are only byte-sized values written through a shared
	 * location, different to the read address
	 */
	if (!config_item->size) {
		if (!config_set->gen4_write_addr)
			return -EFAULT;

		/* Place index in gen4_write_addr + 1 */
		err = scarlett2_usb_set_data(
			mixer, config_set->gen4_write_addr + 1, 1, index);
		if (err < 0)
			return err;

		/* Place value in gen4_write_addr */
		err = scarlett2_usb_set_data(
			mixer, config_set->gen4_write_addr, 1, value);
		if (err < 0)
			return err;

		/* Request the interface do the write */
		return scarlett2_usb_activate_config(
			mixer, config_item->activate);
	}

	/* Not-Gen 4 style needs NVRAM save, supports
	 * bit-modification, and writing is done to the same place
	 * that the value can be read from
	 */

	/* Cancel any pending NVRAM save */
	cancel_delayed_work_sync(&private->work);

	/* Convert config_item->size in bits to size in bytes and
	 * calculate offset
	 */
	if (config_item->size >= 8) {
		size = config_item->size / 8;
		offset = config_item->offset + index * size;

	/* If updating a bit, retrieve the old value, set/clear the
	 * bit as needed, and update value
	 */
	} else {
		u8 tmp;

		size = 1;
		offset = config_item->offset;

		err = scarlett2_usb_get(mixer, offset, &tmp, 1);
		if (err < 0)
			return err;

		if (value)
			tmp |= (1 << index);
		else
			tmp &= ~(1 << index);

		value = tmp;
	}

	/* Send the configuration parameter data */
	err = scarlett2_usb_set_data(mixer, offset, size, value);
	if (err < 0)
		return err;

	/* Activate the change */
	err = scarlett2_usb_activate_config(mixer, config_item->activate);
	if (err < 0)
		return err;

	/* Gen 2 style writes to Gen 4 devices don't need saving */
	if (config_set->gen4_write_addr)
		return 0;

	/* Schedule the change to be written to NVRAM */
	if (config_item->activate != SCARLETT2_USB_CONFIG_SAVE)
		schedule_delayed_work(&private->work, msecs_to_jiffies(2000));

	return 0;
}

/* Send SCARLETT2_USB_DATA_CMD SCARLETT2_USB_CONFIG_SAVE */
static void scarlett2_config_save(struct usb_mixer_interface *mixer)
{
	int err;

	err = scarlett2_usb_activate_config(mixer, SCARLETT2_USB_CONFIG_SAVE);
	if (err < 0)
		usb_audio_err(mixer->chip, "config save failed: %d\n", err);
}

/* Delayed work to save config */
static void scarlett2_config_save_work(struct work_struct *work)
{
	struct scarlett2_data *private =
		container_of(work, struct scarlett2_data, work.work);

	scarlett2_config_save(private->mixer);
}

/* Send a USB message to get sync status; result placed in *sync */
static int scarlett2_usb_get_sync_status(
	struct usb_mixer_interface *mixer,
	u8 *sync)
{
	__le32 data;
	int err;

	err = scarlett2_usb(mixer, SCARLETT2_USB_GET_SYNC,
			    NULL, 0, &data, sizeof(data));
	if (err < 0)
		return err;

	*sync = !!data;
	return 0;
}

/* Return true if the device has a mixer that we can control */
static int scarlett2_has_mixer(struct scarlett2_data *private)
{
	return !!private->info->mux_assignment[0][0].count;
}

/* Map from mixer value to (db + 80) * 2
 * (reverse of scarlett2_mixer_values[])
 */
static int scarlett2_mixer_value_to_db(int value)
{
	int i;

	for (i = 0; i < SCARLETT2_MIXER_VALUE_COUNT; i++)
		if (scarlett2_mixer_values[i] >= value)
			return i;
	return SCARLETT2_MIXER_MAX_VALUE;
}

/* Send a USB message to get the volumes for all inputs of one mix
 * and put the values into private->mix[]
 */
static int scarlett2_usb_get_mix(struct usb_mixer_interface *mixer,
				 int mix_num)
{
	struct scarlett2_data *private = mixer->private_data;

	int num_mixer_in = private->num_mix_in;
	int err, i, j;

	struct {
		__le16 mix_num;
		__le16 count;
	} __packed req;

	__le16 data[SCARLETT2_INPUT_MIX_MAX];

	req.mix_num = cpu_to_le16(mix_num);
	req.count = cpu_to_le16(num_mixer_in);

	err = scarlett2_usb(mixer, SCARLETT2_USB_GET_MIX,
			    &req, sizeof(req),
			    data, num_mixer_in * sizeof(u16));
	if (err < 0)
		return err;

	for (i = 0, j = mix_num * num_mixer_in; i < num_mixer_in; i++, j++)
		private->mix[j] = scarlett2_mixer_value_to_db(
			le16_to_cpu(data[i]));

	return 0;
}

/* Send a USB message to set the volumes for all inputs of one mix
 * (values obtained from private->mix[])
 */
static int scarlett2_usb_set_mix(struct usb_mixer_interface *mixer,
				 int mix_num)
{
	struct scarlett2_data *private = mixer->private_data;

	struct {
		__le16 mix_num;
		__le16 data[SCARLETT2_INPUT_MIX_MAX];
	} __packed req;

	int i, j;
	int num_mixer_in = private->num_mix_in;

	req.mix_num = cpu_to_le16(mix_num);

	for (i = 0, j = mix_num * num_mixer_in; i < num_mixer_in; i++, j++)
		req.data[i] = cpu_to_le16(
			scarlett2_mixer_values[private->mix[j]]
		);

	return scarlett2_usb(mixer, SCARLETT2_USB_SET_MIX,
			     &req, (num_mixer_in + 1) * sizeof(u16),
			     NULL, 0);
}

/* Convert a port number index (per info->port_count) to a hardware ID */
static u32 scarlett2_mux_src_num_to_id(
	const int port_count[][SCARLETT2_PORT_DIRNS], int num)
{
	int port_type;

	for (port_type = 0;
	     port_type < SCARLETT2_PORT_TYPE_COUNT;
	     port_type++) {
		if (num < port_count[port_type][SCARLETT2_PORT_IN])
			return scarlett2_ports[port_type].id | num;
		num -= port_count[port_type][SCARLETT2_PORT_IN];
	}

	/* Oops */
	return 0;
}

/* Convert a hardware ID to a port number index */
static u32 scarlett2_mux_id_to_num(
	const int port_count[][SCARLETT2_PORT_DIRNS], int direction, u32 id)
{
	int port_type;
	int port_num = 0;

	for (port_type = 0;
	     port_type < SCARLETT2_PORT_TYPE_COUNT;
	     port_type++) {
		int base = scarlett2_ports[port_type].id;
		int count = port_count[port_type][direction];

		if (id >= base && id < base + count)
			return port_num + id - base;
		port_num += count;
	}

	/* Oops */
	return -1;
}

/* Convert one mux entry from the interface and load into private->mux[] */
static void scarlett2_usb_populate_mux(struct scarlett2_data *private,
				       u32 mux_entry)
{
	const struct scarlett2_device_info *info = private->info;
	const int (*port_count)[SCARLETT2_PORT_DIRNS] = info->port_count;

	int dst_idx, src_idx;

	dst_idx = scarlett2_mux_id_to_num(port_count, SCARLETT2_PORT_OUT,
					  mux_entry & 0xFFF);
	if (dst_idx < 0)
		return;

	if (dst_idx >= private->num_mux_dsts) {
		usb_audio_err(private->mixer->chip,
			"BUG: scarlett2_mux_id_to_num(%06x, OUT): %d >= %d",
			mux_entry, dst_idx, private->num_mux_dsts);
		return;
	}

	src_idx = scarlett2_mux_id_to_num(port_count, SCARLETT2_PORT_IN,
					  mux_entry >> 12);
	if (src_idx < 0)
		return;

	if (src_idx >= private->num_mux_srcs) {
		usb_audio_err(private->mixer->chip,
			"BUG: scarlett2_mux_id_to_num(%06x, IN): %d >= %d",
			mux_entry, src_idx, private->num_mux_srcs);
		return;
	}

	private->mux[dst_idx] = src_idx;
}

/* Update the meter level map
 *
 * The meter level data from the interface (SCARLETT2_USB_GET_METER
 * request) is returned in mux_assignment order, but to avoid exposing
 * that to userspace, scarlett2_meter_ctl_get() rearranges the data
 * into scarlett2_ports order using the meter_level_map[] array which
 * is set up by this function.
 *
 * In addition, the meter level data values returned from the
 * interface are invalid for destinations where:
 *
 * - the source is "Off"; therefore we set those values to zero (map
 *   value of 255)
 *
 * - the source is assigned to a previous (with respect to the
 *   mux_assignment order) destination; therefore we set those values
 *   to the value previously reported for that source
 */
static void scarlett2_update_meter_level_map(struct scarlett2_data *private)
{
	const struct scarlett2_device_info *info = private->info;
	const struct scarlett2_meter_entry *entry;

	/* sources already assigned to a destination
	 * value is 255 for None, otherwise the value of i
	 * (index into array returned by
	 * scarlett2_usb_get_meter_levels())
	 */
	u8 seen_src[SCARLETT2_MAX_SRCS] = { 1 };
	u8 seen_src_value[SCARLETT2_MAX_SRCS] = { 255 };

	/* index in meter_map[] order */
	int i = 0;

	/* go through the meter_map[] entries */
	for (entry = info->meter_map;
	     entry->count;
	     entry++) {

		/* fill in each meter_level_map[] entry */
		int j, mux_idx;

		for (j = 0, mux_idx = entry->start;
		     j < entry->count;
		     i++, j++, mux_idx++) {

			/* convert mux_idx using line_out_unmap[] */
			int map_mux_idx = (
			    info->line_out_remap_enable &&
			    mux_idx < private->num_line_out
			) ? info->line_out_unmap[mux_idx]
			  : mux_idx;

			/* check which source is connected, and if
			 * that source is already connected elsewhere,
			 * use that existing connection's destination
			 * for this meter entry instead
			 */
			int mux_src = private->mux[mux_idx];

			if (!seen_src[mux_src]) {
				seen_src[mux_src] = 1;
				seen_src_value[mux_src] = i;
			}
			private->meter_level_map[map_mux_idx] =
				seen_src_value[mux_src];
		}
	}
}

/* Send USB message to get mux inputs and then populate private->mux[] */
static int scarlett2_usb_get_mux(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	int count = private->num_mux_dsts;
	int err, i;

	struct {
		__le16 num;
		__le16 count;
	} __packed req;

	__le32 data[SCARLETT2_MUX_MAX];

	private->mux_updated = 0;

	req.num = 0;
	req.count = cpu_to_le16(count);

	err = scarlett2_usb(mixer, SCARLETT2_USB_GET_MUX,
			    &req, sizeof(req),
			    data, count * sizeof(u32));
	if (err < 0)
		return err;

	for (i = 0; i < count; i++)
		scarlett2_usb_populate_mux(private, le32_to_cpu(data[i]));

	scarlett2_update_meter_level_map(private);

	return 0;
}

/* Send USB messages to set mux inputs */
static int scarlett2_usb_set_mux(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	const int (*port_count)[SCARLETT2_PORT_DIRNS] = info->port_count;
	int table;

	struct {
		__le16 pad;
		__le16 num;
		__le32 data[SCARLETT2_MUX_MAX];
	} __packed req;

	req.pad = 0;

	/* set mux settings for each rate */
	for (table = 0; table < SCARLETT2_MUX_TABLES; table++) {
		const struct scarlett2_mux_entry *entry;

		/* i counts over the output array */
		int i = 0, err;

		req.num = cpu_to_le16(table);

		/* loop through each entry */
		for (entry = info->mux_assignment[table];
		     entry->count;
		     entry++) {
			int j;
			int port_type = entry->port_type;
			int port_idx = entry->start;
			int mux_idx = scarlett2_get_port_start_num(port_count,
				SCARLETT2_PORT_OUT, port_type) + port_idx;
			int dst_id = scarlett2_ports[port_type].id + port_idx;

			/* Empty slots */
			if (!dst_id) {
				for (j = 0; j < entry->count; j++)
					req.data[i++] = 0;
				continue;
			}

			/* Non-empty mux slots use the lower 12 bits
			 * for the destination and next 12 bits for
			 * the source
			 */
			for (j = 0; j < entry->count; j++) {
				int src_id = scarlett2_mux_src_num_to_id(
					port_count, private->mux[mux_idx++]);
				req.data[i++] = cpu_to_le32(dst_id |
							    src_id << 12);
				dst_id++;
			}
		}

		err = scarlett2_usb(mixer, SCARLETT2_USB_SET_MUX,
				    &req, (i + 1) * sizeof(u32),
				    NULL, 0);
		if (err < 0)
			return err;
	}

	scarlett2_update_meter_level_map(private);

	return 0;
}

/* Send USB message to get meter levels */
static int scarlett2_usb_get_meter_levels(struct usb_mixer_interface *mixer,
					  u16 num_meters, u16 *levels)
{
	struct {
		__le16 pad;
		__le16 num_meters;
		__le32 magic;
	} __packed req;
	__le32 resp[SCARLETT2_MAX_METERS];
	int i, err;

	req.pad = 0;
	req.num_meters = cpu_to_le16(num_meters);
	req.magic = cpu_to_le32(SCARLETT2_USB_METER_LEVELS_GET_MAGIC);
	err = scarlett2_usb(mixer, SCARLETT2_USB_GET_METER,
			    &req, sizeof(req), resp, num_meters * sizeof(u32));
	if (err < 0)
		return err;

	/* copy, convert to u16 */
	for (i = 0; i < num_meters; i++)
		levels[i] = le32_to_cpu(resp[i]);

	return 0;
}

/* For config items with mute=1, xor bits 0 & 1 together to get the
 * current/next state. This won't have any effect on values which are
 * only ever 0/1.
 */
static uint8_t scarlett2_decode_muteable(uint8_t v)
{
	return (v ^ (v >> 1)) & 1;
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

	/* We set USB_MIXER_BESPOKEN type, so that the core USB mixer code
	 * ignores them for resume and other operations.
	 * Also, the head.id field is set to 0, as we don't use this field.
	 */
	elem->head.mixer = mixer;
	elem->control = index;
	elem->head.id = 0;
	elem->channels = channels;
	elem->val_type = USB_MIXER_BESPOKEN;

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

/*** Firmware Version Control ***/

static int scarlett2_firmware_version_ctl_get(
	struct snd_kcontrol *kctl,
	struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct scarlett2_data *private = elem->head.mixer->private_data;

	ucontrol->value.integer.value[0] = private->firmware_version;

	return 0;
}

static int scarlett2_firmware_version_ctl_info(
	struct snd_kcontrol *kctl,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;

	return 0;
}

static const struct snd_kcontrol_new scarlett2_firmware_version_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.name = "",
	.info = scarlett2_firmware_version_ctl_info,
	.get  = scarlett2_firmware_version_ctl_get
};

static int scarlett2_add_firmware_version_ctl(
	struct usb_mixer_interface *mixer)
{
	return scarlett2_add_new_ctl(mixer, &scarlett2_firmware_version_ctl,
				     0, 0, "Firmware Version", NULL);
}

/*** Minimum Firmware Version Control ***/

static int scarlett2_min_firmware_version_ctl_get(
	struct snd_kcontrol *kctl,
	struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct scarlett2_data *private = elem->head.mixer->private_data;

	ucontrol->value.integer.value[0] = private->info->min_firmware_version;

	return 0;
}

static int scarlett2_min_firmware_version_ctl_info(
	struct snd_kcontrol *kctl,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;

	return 0;
}

static const struct snd_kcontrol_new scarlett2_min_firmware_version_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.name = "",
	.info = scarlett2_min_firmware_version_ctl_info,
	.get  = scarlett2_min_firmware_version_ctl_get
};

static int scarlett2_add_min_firmware_version_ctl(
	struct usb_mixer_interface *mixer)
{
	return scarlett2_add_new_ctl(mixer, &scarlett2_min_firmware_version_ctl,
				     0, 0, "Minimum Firmware Version", NULL);
}

/*** Sync Control ***/

/* Update sync control after receiving notification that the status
 * has changed
 */
static int scarlett2_update_sync(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;

	private->sync_updated = 0;
	return scarlett2_usb_get_sync_status(mixer, &private->sync);
}

static int scarlett2_sync_ctl_info(struct snd_kcontrol *kctl,
				   struct snd_ctl_elem_info *uinfo)
{
	static const char *texts[2] = {
		"Unlocked", "Locked"
	};
	return snd_ctl_enum_info(uinfo, 1, 2, texts);
}

static int scarlett2_sync_ctl_get(struct snd_kcontrol *kctl,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	if (private->sync_updated) {
		err = scarlett2_update_sync(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.enumerated.item[0] = private->sync;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_sync_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.name = "",
	.info = scarlett2_sync_ctl_info,
	.get  = scarlett2_sync_ctl_get
};

static int scarlett2_add_sync_ctl(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;

	/* devices without a mixer also don't support reporting sync status */
	if (!scarlett2_has_mixer(private))
		return 0;

	return scarlett2_add_new_ctl(mixer, &scarlett2_sync_ctl,
				     0, 1, "Sync Status", &private->sync_ctl);
}

/*** Autogain Switch and Status Controls ***/

/* Forward declarations as phantom power and autogain can disable each other */
static int scarlett2_check_input_phantom_updated(struct usb_mixer_interface *);
static int scarlett2_phantom_is_switching(struct scarlett2_data *, int);

/* Set the access mode of a control to read-only (val = 0) or
 * read-write (val = 1).
 */
static void scarlett2_set_ctl_access(struct snd_kcontrol *kctl, int val)
{
	if (val)
		kctl->vd[0].access |= SNDRV_CTL_ELEM_ACCESS_WRITE;
	else
		kctl->vd[0].access &= ~SNDRV_CTL_ELEM_ACCESS_WRITE;
}

/* Check if autogain is running on any input */
static int scarlett2_autogain_is_running(struct scarlett2_data *private)
{
	int i;

	/* autogain_status[] is 0 if autogain is running */
	for (i = 0; i < private->info->gain_input_count; i++)
		if (!private->autogain_status[i])
			return 1;

	return 0;
}

static int scarlett2_update_autogain(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int err, i;
	u8 raw_autogain_status[SCARLETT2_INPUT_GAIN_MAX];

	private->autogain_updated = 0;

	if (!info->gain_input_count)
		return 0;

	err = scarlett2_usb_get_config(
		mixer, SCARLETT2_CONFIG_AUTOGAIN_SWITCH,
		info->gain_input_count, private->autogain_switch);
	if (err < 0)
		return err;
	err = scarlett2_usb_get_config(
		mixer, SCARLETT2_CONFIG_AUTOGAIN_STATUS,
		info->gain_input_count, raw_autogain_status);
	if (err < 0)
		return err;

	/* Translate autogain_switch and raw_autogain_status into
	 * autogain_status.
	 *
	 * When autogain_switch[] is set, the status is the first
	 * element in scarlett2_autogain_status_texts[] (Running). The
	 * subsequent elements correspond to the status value from the
	 * device (raw_autogain_status[]) + 1. The last element is
	 * "Invalid", in case the device reports a status outside the
	 * range of scarlett2_autogain_status_texts[].
	 */
	for (i = 0; i < info->gain_input_count; i++)
		if (private->autogain_switch[i])
			private->autogain_status[i] = 0;
		else if (raw_autogain_status[i] <
				ARRAY_SIZE(scarlett2_autogain_status_texts) - 1)
			private->autogain_status[i] =
				raw_autogain_status[i] + 1;
		else
			private->autogain_status[i] =
				ARRAY_SIZE(scarlett2_autogain_status_texts) - 1;

	return 0;
}

/* Update access mode for controls affected by autogain */
static void scarlett2_autogain_update_access(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int val = !scarlett2_autogain_is_running(private);
	int i;

	scarlett2_set_ctl_access(private->input_select_ctl, val);
	for (i = 0; i < info->gain_input_count / 2; i++)
		scarlett2_set_ctl_access(private->input_link_ctls[i], val);
	for (i = 0; i < info->gain_input_count; i++) {
		scarlett2_set_ctl_access(private->input_gain_ctls[i], val);
		scarlett2_set_ctl_access(private->safe_ctls[i], val);
	}
	for (i = 0; i < info->level_input_count; i++)
		scarlett2_set_ctl_access(private->level_ctls[i], val);
	for (i = 0; i < info->air_input_count; i++)
		scarlett2_set_ctl_access(private->air_ctls[i], val);
	for (i = 0; i < info->phantom_count; i++)
		scarlett2_set_ctl_access(private->phantom_ctls[i], val);
}

/* Notify of access mode change for all controls read-only while
 * autogain runs.
 */
static void scarlett2_autogain_notify_access(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int i;

	snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_INFO,
		       &private->input_select_ctl->id);
	for (i = 0; i < info->gain_input_count / 2; i++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_INFO,
			       &private->input_link_ctls[i]->id);
	for (i = 0; i < info->gain_input_count; i++) {
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_INFO,
			       &private->input_gain_ctls[i]->id);
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_INFO,
			       &private->safe_ctls[i]->id);
	}
	for (i = 0; i < info->level_input_count; i++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_INFO,
			       &private->level_ctls[i]->id);
	for (i = 0; i < info->air_input_count; i++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_INFO,
			       &private->air_ctls[i]->id);
	for (i = 0; i < info->phantom_count; i++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_INFO,
			       &private->phantom_ctls[i]->id);
}

/* Call scarlett2_update_autogain() and
 * scarlett2_autogain_update_access() if autogain_updated is set.
 */
static int scarlett2_check_autogain_updated(
	struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	int err;

	if (!private->autogain_updated)
		return 0;

	err = scarlett2_update_autogain(mixer);
	if (err < 0)
		return err;

	scarlett2_autogain_update_access(mixer);

	return 0;
}

/* If autogain_updated is set when a *_ctl_put() function for a
 * control that is meant to be read-only while autogain is running,
 * update the autogain status and access mode of affected controls.
 * Return -EPERM if autogain is running.
 */
static int scarlett2_check_put_during_autogain(
	struct usb_mixer_interface *mixer)
{
	int err = scarlett2_check_autogain_updated(mixer);

	if (err < 0)
		return err;

	if (scarlett2_autogain_is_running(mixer->private_data))
		return -EPERM;

	return 0;
}

static int scarlett2_autogain_switch_ctl_info(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_info *uinfo)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err;

	mutex_lock(&private->data_mutex);

	err = scarlett2_check_input_phantom_updated(mixer);
	if (err < 0)
		goto unlock;

	err = snd_ctl_boolean_mono_info(kctl, uinfo);

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_autogain_switch_ctl_get(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	err = scarlett2_check_autogain_updated(mixer);
	if (err < 0)
		goto unlock;

	ucontrol->value.enumerated.item[0] =
		private->autogain_switch[elem->control];

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_autogain_status_ctl_get(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	err = scarlett2_check_autogain_updated(mixer);
	if (err < 0)
		goto unlock;

	ucontrol->value.enumerated.item[0] =
		private->autogain_status[elem->control];

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_autogain_switch_ctl_put(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;

	int index = elem->control;
	int oval, val, err;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	err = scarlett2_check_input_phantom_updated(mixer);
	if (err < 0)
		goto unlock;

	if (scarlett2_phantom_is_switching(private, index)) {
		err = -EPERM;
		goto unlock;
	}

	oval = private->autogain_switch[index];
	val = !!ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->autogain_switch[index] = val;

	/* Send switch change to the device */
	err = scarlett2_usb_set_config(
		mixer, SCARLETT2_CONFIG_AUTOGAIN_SWITCH, index, val);
	if (err == 0)
		err = 1;

	scarlett2_autogain_update_access(mixer);
	scarlett2_autogain_notify_access(mixer);

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_autogain_status_ctl_info(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_info *uinfo)
{
	return snd_ctl_enum_info(
		uinfo, 1,
		ARRAY_SIZE(scarlett2_autogain_status_texts),
		scarlett2_autogain_status_texts);
}

static const struct snd_kcontrol_new scarlett2_autogain_switch_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = scarlett2_autogain_switch_ctl_info,
	.get  = scarlett2_autogain_switch_ctl_get,
	.put  = scarlett2_autogain_switch_ctl_put
};

static const struct snd_kcontrol_new scarlett2_autogain_status_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.name = "",
	.info = scarlett2_autogain_status_ctl_info,
	.get  = scarlett2_autogain_status_ctl_get,
};

/*** Input Select Control ***/

static int scarlett2_update_input_select(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int link_count = info->gain_input_count / 2;
	int err;

	private->input_select_updated = 0;

	if (!link_count)
		return 0;

	err = scarlett2_usb_get_config(
		mixer, SCARLETT2_CONFIG_INPUT_SELECT_SWITCH,
		1, &private->input_select_switch);
	if (err < 0)
		return err;

	err = scarlett2_usb_get_config(
		mixer, SCARLETT2_CONFIG_INPUT_LINK_SWITCH,
		link_count, private->input_link_switch);
	if (err < 0)
		return err;

	/* simplified because no model yet has link_count > 1 */
	if (private->input_link_switch[0])
		private->input_select_switch = 0;

	return 0;
}

static int scarlett2_input_select_ctl_get(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	if (private->input_select_updated) {
		err = scarlett2_update_input_select(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.enumerated.item[0] = private->input_select_switch;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_input_select_ctl_put(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;

	int oval, val, err;
	int max_val = private->input_link_switch[0] ? 0 : 1;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	err = scarlett2_check_put_during_autogain(mixer);
	if (err < 0)
		goto unlock;

	oval = private->input_select_switch;
	val = ucontrol->value.integer.value[0];

	if (val < 0)
		val = 0;
	else if (val > max_val)
		val = max_val;

	if (oval == val)
		goto unlock;

	private->input_select_switch = val;

	/* Send switch change to the device if inputs not linked */
	if (!private->input_link_switch[0])
		err = scarlett2_usb_set_config(
			mixer, SCARLETT2_CONFIG_INPUT_SELECT_SWITCH,
			1, val);
	if (err == 0)
		err = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_input_select_ctl_info(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_info *uinfo)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;

	int inputs = private->info->gain_input_count;
	int i, j;
	int err;
	char **values = kcalloc(inputs, sizeof(char *), GFP_KERNEL);

	if (!values)
		return -ENOMEM;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	err = scarlett2_check_autogain_updated(mixer);
	if (err < 0)
		goto unlock;

	/* Loop through each input
	 * Linked inputs have one value for the pair
	 */
	for (i = 0, j = 0; i < inputs; i++) {
		if (private->input_link_switch[i / 2]) {
			values[j++] = kasprintf(
				GFP_KERNEL, "Input %d-%d", i + 1, i + 2);
			i++;
		} else {
			values[j++] = kasprintf(
				GFP_KERNEL, "Input %d", i + 1);
		}
	}

	err = snd_ctl_enum_info(uinfo, 1, j,
				(const char * const *)values);

unlock:
	mutex_unlock(&private->data_mutex);

	for (i = 0; i < inputs; i++)
		kfree(values[i]);
	kfree(values);

	return err;
}

static const struct snd_kcontrol_new scarlett2_input_select_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = scarlett2_input_select_ctl_info,
	.get  = scarlett2_input_select_ctl_get,
	.put  = scarlett2_input_select_ctl_put,
};

/*** Input Link Switch Controls ***/

/* snd_ctl_boolean_mono_info() with autogain-updated check
 * (for controls that are read-only while autogain is running)
 */
static int scarlett2_autogain_disables_ctl_info(struct snd_kcontrol *kctl,
						struct snd_ctl_elem_info *uinfo)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	err = scarlett2_check_autogain_updated(mixer);
	if (err < 0)
		goto unlock;

	err = snd_ctl_boolean_mono_info(kctl, uinfo);

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_input_link_ctl_get(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	if (private->input_select_updated) {
		err = scarlett2_update_input_select(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.enumerated.item[0] =
		private->input_link_switch[elem->control];

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_input_link_ctl_put(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;

	int index = elem->control;
	int oval, val, err;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	err = scarlett2_check_put_during_autogain(mixer);
	if (err < 0)
		goto unlock;

	oval = private->input_link_switch[index];
	val = !!ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->input_link_switch[index] = val;

	/* Notify of change in input select options available */
	snd_ctl_notify(mixer->chip->card,
		       SNDRV_CTL_EVENT_MASK_VALUE | SNDRV_CTL_EVENT_MASK_INFO,
		       &private->input_select_ctl->id);
	private->input_select_updated = 1;

	/* Send switch change to the device
	 * Link for channels 1-2 is at index 1
	 * No device yet has more than 2 channels linked
	 */
	err = scarlett2_usb_set_config(
		mixer, SCARLETT2_CONFIG_INPUT_LINK_SWITCH, index + 1, val);
	if (err == 0)
		err = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_input_link_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = scarlett2_autogain_disables_ctl_info,
	.get  = scarlett2_input_link_ctl_get,
	.put  = scarlett2_input_link_ctl_put
};

/*** Input Gain Controls ***/

static int scarlett2_update_input_gain(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;

	private->input_gain_updated = 0;

	if (!info->gain_input_count)
		return 0;

	return scarlett2_usb_get_config(
		mixer, SCARLETT2_CONFIG_INPUT_GAIN,
		info->gain_input_count, private->gain);
}

static int scarlett2_input_gain_ctl_info(struct snd_kcontrol *kctl,
					 struct snd_ctl_elem_info *uinfo)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	err = scarlett2_check_autogain_updated(mixer);
	if (err < 0)
		goto unlock;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = elem->channels;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = SCARLETT2_MAX_GAIN_VALUE;
	uinfo->value.integer.step = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_input_gain_ctl_get(struct snd_kcontrol *kctl,
					struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	if (private->input_gain_updated) {
		err = scarlett2_update_input_gain(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.integer.value[0] =
		private->gain[elem->control];

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_input_gain_ctl_put(struct snd_kcontrol *kctl,
					struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;

	int index = elem->control;
	int oval, val, err;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	err = scarlett2_check_put_during_autogain(mixer);
	if (err < 0)
		goto unlock;

	oval = private->gain[index];
	val = ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->gain[index] = val;

	/* Send gain change to the device */
	err = scarlett2_usb_set_config(mixer, SCARLETT2_CONFIG_INPUT_GAIN,
				       index, val);
	if (err == 0)
		err = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const DECLARE_TLV_DB_MINMAX(
	db_scale_scarlett2_gain, 0, SCARLETT2_MAX_GAIN_DB * 100
);

static const struct snd_kcontrol_new scarlett2_input_gain_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
		  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.name = "",
	.info = scarlett2_input_gain_ctl_info,
	.get  = scarlett2_input_gain_ctl_get,
	.put  = scarlett2_input_gain_ctl_put,
	.private_value = 0, /* max value */
	.tlv = { .p = db_scale_scarlett2_gain }
};

/*** Safe Controls ***/

static int scarlett2_update_input_safe(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;

	private->input_safe_updated = 0;

	if (!info->gain_input_count)
		return 0;

	return scarlett2_usb_get_config(
		mixer, SCARLETT2_CONFIG_SAFE_SWITCH,
		info->gain_input_count, private->safe_switch);
}

static int scarlett2_safe_ctl_get(struct snd_kcontrol *kctl,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	if (private->input_safe_updated) {
		err = scarlett2_update_input_safe(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.integer.value[0] =
		private->safe_switch[elem->control];

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_safe_ctl_put(struct snd_kcontrol *kctl,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;

	int index = elem->control;
	int oval, val, err;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	err = scarlett2_check_put_during_autogain(mixer);
	if (err < 0)
		goto unlock;

	oval = private->safe_switch[index];
	val = !!ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->safe_switch[index] = val;

	/* Send switch change to the device */
	err = scarlett2_usb_set_config(mixer, SCARLETT2_CONFIG_SAFE_SWITCH,
				       index, val);
	if (err == 0)
		err = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_safe_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = scarlett2_autogain_disables_ctl_info,
	.get  = scarlett2_safe_ctl_get,
	.put  = scarlett2_safe_ctl_put,
};

/*** PCM Input Control ***/

static int scarlett2_update_pcm_input_switch(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	int err;

	private->pcm_input_switch_updated = 0;

	err = scarlett2_usb_get_config(
		mixer, SCARLETT2_CONFIG_PCM_INPUT_SWITCH,
		1, &private->pcm_input_switch);
	if (err < 0)
		return err;

	return 0;
}

static int scarlett2_pcm_input_switch_ctl_get(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = elem->head.mixer->private_data;
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->pcm_input_switch_updated) {
		err = scarlett2_update_pcm_input_switch(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.enumerated.item[0] = private->pcm_input_switch;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_pcm_input_switch_ctl_put(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;

	int oval, val, err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	oval = private->pcm_input_switch;
	val = !!ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->pcm_input_switch = val;

	/* Send switch change to the device */
	err = scarlett2_usb_set_config(
		mixer, SCARLETT2_CONFIG_PCM_INPUT_SWITCH,
		0, val);
	if (err == 0)
		err = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_pcm_input_switch_ctl_info(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_info *uinfo)
{
	static const char *const values[2] = {
		"Direct", "Mixer"
	};

	return snd_ctl_enum_info(
		uinfo, 1, 2, values);
}

static const struct snd_kcontrol_new scarlett2_pcm_input_switch_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = scarlett2_pcm_input_switch_ctl_info,
	.get  = scarlett2_pcm_input_switch_ctl_get,
	.put  = scarlett2_pcm_input_switch_ctl_put
};

/*** Analogue Line Out Volume Controls ***/

/* Update hardware volume controls after receiving notification that
 * they have changed
 */
static int scarlett2_update_volumes(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	s16 vol;
	int err, i;

	private->vol_updated = 0;

	if (scarlett2_has_config_item(private,
				      SCARLETT2_CONFIG_MASTER_VOLUME)) {
		err = scarlett2_usb_get_config(
			mixer, SCARLETT2_CONFIG_MASTER_VOLUME,
			1, &vol);
		if (err < 0)
			return err;

		private->master_vol = clamp(vol + SCARLETT2_VOLUME_BIAS,
					    0, SCARLETT2_VOLUME_BIAS);

		if (scarlett2_has_config_item(private,
					      SCARLETT2_CONFIG_SW_HW_SWITCH))
			for (i = 0; i < private->num_line_out; i++)
				if (private->vol_sw_hw_switch[i])
					private->vol[i] = private->master_vol;
	}

	if (scarlett2_has_config_item(private,
				      SCARLETT2_CONFIG_HEADPHONE_VOLUME)) {
		err = scarlett2_usb_get_config(
			mixer, SCARLETT2_CONFIG_HEADPHONE_VOLUME,
			1, &vol);
		if (err < 0)
			return err;

		private->headphone_vol = clamp(vol + SCARLETT2_VOLUME_BIAS,
					       0, SCARLETT2_VOLUME_BIAS);
	}

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
	struct scarlett2_data *private = mixer->private_data;
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	if (private->vol_updated) {
		err = scarlett2_update_volumes(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.integer.value[0] = private->master_vol;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_headphone_volume_ctl_get(
	struct snd_kcontrol *kctl,
	struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	if (private->vol_updated) {
		err = scarlett2_update_volumes(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.integer.value[0] = private->headphone_vol;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int line_out_remap(struct scarlett2_data *private, int index)
{
	const struct scarlett2_device_info *info = private->info;

	if (!info->line_out_remap_enable)
		return index;

	if (index >= private->num_line_out)
		return index;

	return info->line_out_remap[index];
}

static int scarlett2_volume_ctl_get(struct snd_kcontrol *kctl,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int index = line_out_remap(private, elem->control);
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	if (private->vol_updated) {
		err = scarlett2_update_volumes(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.integer.value[0] = private->vol[index];

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_volume_ctl_put(struct snd_kcontrol *kctl,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int index = line_out_remap(private, elem->control);
	int oval, val, err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

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
	db_scale_scarlett2_volume, -SCARLETT2_VOLUME_BIAS * 100, 0
);

static const struct snd_kcontrol_new scarlett2_master_volume_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READ |
		  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.name = "",
	.info = scarlett2_volume_ctl_info,
	.get  = scarlett2_master_volume_ctl_get,
	.private_value = 0, /* max value */
	.tlv = { .p = db_scale_scarlett2_volume }
};

static const struct snd_kcontrol_new scarlett2_headphone_volume_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READ |
		  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.name = "",
	.info = scarlett2_volume_ctl_info,
	.get  = scarlett2_headphone_volume_ctl_get,
	.private_value = 0, /* max value */
	.tlv = { .p = db_scale_scarlett2_volume }
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
	.tlv = { .p = db_scale_scarlett2_volume }
};

/*** Mute Switch Controls ***/

static int scarlett2_update_dim_mute(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	int err, i;
	u8 mute;

	private->dim_mute_updated = 0;

	if (!scarlett2_has_config_item(private, SCARLETT2_CONFIG_SW_HW_SWITCH))
		return 0;

	err = scarlett2_usb_get_config(
		mixer, SCARLETT2_CONFIG_DIM_MUTE,
		SCARLETT2_DIM_MUTE_COUNT, private->dim_mute);
	if (err < 0)
		return err;

	for (i = 0; i < SCARLETT2_DIM_MUTE_COUNT; i++)
		private->dim_mute[i] = !!private->dim_mute[i];

	mute = private->dim_mute[SCARLETT2_BUTTON_MUTE];

	for (i = 0; i < private->num_line_out; i++)
		if (private->vol_sw_hw_switch[i])
			private->mute_switch[i] = mute;

	return 0;
}

static int scarlett2_mute_ctl_get(struct snd_kcontrol *kctl,
					struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int index = line_out_remap(private, elem->control);
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	if (private->dim_mute_updated) {
		err = scarlett2_update_dim_mute(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.integer.value[0] = private->mute_switch[index];

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_mute_ctl_put(struct snd_kcontrol *kctl,
					struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int index = line_out_remap(private, elem->control);
	int oval, val, err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	oval = private->mute_switch[index];
	val = !!ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->mute_switch[index] = val;

	/* Send mute change to the device */
	err = scarlett2_usb_set_config(mixer, SCARLETT2_CONFIG_MUTE_SWITCH,
				       index, val);
	if (err == 0)
		err = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_mute_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = snd_ctl_boolean_mono_info,
	.get  = scarlett2_mute_ctl_get,
	.put  = scarlett2_mute_ctl_put,
};

/*** HW/SW Volume Switch Controls ***/

static void scarlett2_sw_hw_ctl_ro(struct scarlett2_data *private, int index)
{
	private->sw_hw_ctls[index]->vd[0].access &=
		~SNDRV_CTL_ELEM_ACCESS_WRITE;
}

static void scarlett2_sw_hw_ctl_rw(struct scarlett2_data *private, int index)
{
	private->sw_hw_ctls[index]->vd[0].access |=
		SNDRV_CTL_ELEM_ACCESS_WRITE;
}

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
	struct scarlett2_data *private = elem->head.mixer->private_data;
	int index = line_out_remap(private, elem->control);

	ucontrol->value.enumerated.item[0] = private->vol_sw_hw_switch[index];
	return 0;
}

static void scarlett2_vol_ctl_set_writable(struct usb_mixer_interface *mixer,
					   int index, int value)
{
	struct scarlett2_data *private = mixer->private_data;
	struct snd_card *card = mixer->chip->card;

	/* Set/Clear write bits */
	if (value) {
		private->vol_ctls[index]->vd[0].access |=
			SNDRV_CTL_ELEM_ACCESS_WRITE;
		private->mute_ctls[index]->vd[0].access |=
			SNDRV_CTL_ELEM_ACCESS_WRITE;
	} else {
		private->vol_ctls[index]->vd[0].access &=
			~SNDRV_CTL_ELEM_ACCESS_WRITE;
		private->mute_ctls[index]->vd[0].access &=
			~SNDRV_CTL_ELEM_ACCESS_WRITE;
	}

	/* Notify of write bit and possible value change */
	snd_ctl_notify(card,
		       SNDRV_CTL_EVENT_MASK_VALUE | SNDRV_CTL_EVENT_MASK_INFO,
		       &private->vol_ctls[index]->id);
	snd_ctl_notify(card,
		       SNDRV_CTL_EVENT_MASK_VALUE | SNDRV_CTL_EVENT_MASK_INFO,
		       &private->mute_ctls[index]->id);
}

static int scarlett2_sw_hw_change(struct usb_mixer_interface *mixer,
				  int ctl_index, int val)
{
	struct scarlett2_data *private = mixer->private_data;
	int index = line_out_remap(private, ctl_index);
	int err;

	private->vol_sw_hw_switch[index] = val;

	/* Change access mode to RO (hardware controlled volume)
	 * or RW (software controlled volume)
	 */
	scarlett2_vol_ctl_set_writable(mixer, ctl_index, !val);

	/* Reset volume/mute to master volume/mute */
	private->vol[index] = private->master_vol;
	private->mute_switch[index] = private->dim_mute[SCARLETT2_BUTTON_MUTE];

	/* Set SW volume to current HW volume */
	err = scarlett2_usb_set_config(
		mixer, SCARLETT2_CONFIG_LINE_OUT_VOLUME,
		index, private->master_vol - SCARLETT2_VOLUME_BIAS);
	if (err < 0)
		return err;

	/* Set SW mute to current HW mute */
	err = scarlett2_usb_set_config(
		mixer, SCARLETT2_CONFIG_MUTE_SWITCH,
		index, private->dim_mute[SCARLETT2_BUTTON_MUTE]);
	if (err < 0)
		return err;

	/* Send SW/HW switch change to the device */
	return scarlett2_usb_set_config(mixer, SCARLETT2_CONFIG_SW_HW_SWITCH,
					index, val);
}

static int scarlett2_sw_hw_enum_ctl_put(struct snd_kcontrol *kctl,
					struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int ctl_index = elem->control;
	int index = line_out_remap(private, ctl_index);
	int oval, val, err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	oval = private->vol_sw_hw_switch[index];
	val = !!ucontrol->value.enumerated.item[0];

	if (oval == val)
		goto unlock;

	err = scarlett2_sw_hw_change(mixer, ctl_index, val);
	if (err == 0)
		err = 1;

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

static int scarlett2_update_input_level(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;

	private->input_level_updated = 0;

	if (!info->level_input_count)
		return 0;

	return scarlett2_usb_get_config(
		mixer, SCARLETT2_CONFIG_LEVEL_SWITCH,
		info->level_input_count + info->level_input_first,
		private->level_switch);
}

static int scarlett2_level_enum_ctl_info(struct snd_kcontrol *kctl,
					 struct snd_ctl_elem_info *uinfo)
{
	static const char *const values[2] = {
		"Line", "Inst"
	};
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	err = scarlett2_check_autogain_updated(mixer);
	if (err < 0)
		goto unlock;

	err = snd_ctl_enum_info(uinfo, 1, 2, values);

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_level_enum_ctl_get(struct snd_kcontrol *kctl,
					struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;

	int index = elem->control + info->level_input_first;
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	if (private->input_level_updated) {
		err = scarlett2_update_input_level(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.enumerated.item[0] = scarlett2_decode_muteable(
		private->level_switch[index]);

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_level_enum_ctl_put(struct snd_kcontrol *kctl,
					struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;

	int index = elem->control + info->level_input_first;
	int oval, val, err;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	err = scarlett2_check_put_during_autogain(mixer);
	if (err < 0)
		goto unlock;

	oval = private->level_switch[index];
	val = !!ucontrol->value.enumerated.item[0];

	if (oval == val)
		goto unlock;

	private->level_switch[index] = val;

	/* To set the Gen 4 muteable controls, bit 1 gets set instead */
	if (private->config_set->items[SCARLETT2_CONFIG_LEVEL_SWITCH].mute)
		val = (!val) | 0x02;

	/* Send switch change to the device */
	err = scarlett2_usb_set_config(mixer, SCARLETT2_CONFIG_LEVEL_SWITCH,
				       index, val);
	if (err == 0)
		err = 1;

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

static int scarlett2_update_input_pad(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;

	private->input_pad_updated = 0;

	if (!info->pad_input_count)
		return 0;

	return scarlett2_usb_get_config(
		mixer, SCARLETT2_CONFIG_PAD_SWITCH,
		info->pad_input_count, private->pad_switch);
}

static int scarlett2_pad_ctl_get(struct snd_kcontrol *kctl,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	if (private->input_pad_updated) {
		err = scarlett2_update_input_pad(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.integer.value[0] =
		private->pad_switch[elem->control];

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_pad_ctl_put(struct snd_kcontrol *kctl,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;

	int index = elem->control;
	int oval, val, err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	oval = private->pad_switch[index];
	val = !!ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->pad_switch[index] = val;

	/* Send switch change to the device */
	err = scarlett2_usb_set_config(mixer, SCARLETT2_CONFIG_PAD_SWITCH,
				       index, val);
	if (err == 0)
		err = 1;

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

/*** Air Switch Controls ***/

static int scarlett2_update_input_air(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;

	private->input_air_updated = 0;

	if (!info->air_input_count)
		return 0;

	return scarlett2_usb_get_config(
		mixer, SCARLETT2_CONFIG_AIR_SWITCH,
		info->air_input_count, private->air_switch);
}

static int scarlett2_air_ctl_get(struct snd_kcontrol *kctl,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	if (private->input_air_updated) {
		err = scarlett2_update_input_air(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.integer.value[0] = private->air_switch[elem->control];

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_air_ctl_put(struct snd_kcontrol *kctl,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;

	int index = elem->control;
	int oval, val, err;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	err = scarlett2_check_put_during_autogain(mixer);
	if (err < 0)
		goto unlock;

	oval = private->air_switch[index];
	val = ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->air_switch[index] = val;

	/* Send switch change to the device */
	err = scarlett2_usb_set_config(mixer, SCARLETT2_CONFIG_AIR_SWITCH,
				       index, val);
	if (err == 0)
		err = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_air_with_drive_ctl_info(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_info *uinfo)
{
	static const char *const values[3] = {
		"Off", "Presence", "Presence + Drive"
	};
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	err = scarlett2_check_autogain_updated(mixer);
	if (err < 0)
		goto unlock;

	err = snd_ctl_enum_info(uinfo, 1, 3, values);

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_air_ctl[2] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "",
		.info = snd_ctl_boolean_mono_info,
		.get  = scarlett2_air_ctl_get,
		.put  = scarlett2_air_ctl_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "",
		.info = scarlett2_air_with_drive_ctl_info,
		.get  = scarlett2_air_ctl_get,
		.put  = scarlett2_air_ctl_put,
	}
};

/*** Phantom Switch Controls ***/

static int scarlett2_update_input_phantom(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int err;

	private->input_phantom_updated = 0;

	if (!info->phantom_count)
		return 0;

	err = scarlett2_usb_get_config(
		mixer, SCARLETT2_CONFIG_PHANTOM_SWITCH,
		info->phantom_count, private->phantom_switch);
	if (err < 0)
		return err;

	if (scarlett2_has_config_item(private,
				      SCARLETT2_CONFIG_PHANTOM_PERSISTENCE)) {
		err = scarlett2_usb_get_config(
			mixer, SCARLETT2_CONFIG_PHANTOM_PERSISTENCE,
			1, &private->phantom_persistence);
		if (err < 0)
			return err;
	}

	return 0;
}

/* Check if phantom power on the given input is currently changing state */
static int scarlett2_phantom_is_switching(
	struct scarlett2_data *private, int line_num)
{
	const struct scarlett2_device_info *info = private->info;
	int index = line_num / info->inputs_per_phantom;

	return !!(private->phantom_switch[index] & 0x02);
}

/* Update autogain controls' access mode when phantom power changes state */
static void scarlett2_phantom_update_access(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int i;

	/* Disable autogain controls if phantom power is changing state */
	for (i = 0; i < info->gain_input_count; i++) {
		int val = !scarlett2_phantom_is_switching(private, i);

		scarlett2_set_ctl_access(private->autogain_ctls[i], val);
	}
}

/* Notify of access mode change for autogain which can't be enabled
 * while phantom power is changing.
 */
static void scarlett2_phantom_notify_access(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int i;

	for (i = 0; i < info->gain_input_count; i++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_INFO,
			       &private->autogain_ctls[i]->id);
}

/* Call scarlett2_update_input_phantom() and
 * scarlett2_phantom_update_access() if input_phantom_updated is set.
 */
static int scarlett2_check_input_phantom_updated(
	struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	int err;

	if (!private->input_phantom_updated)
		return 0;

	err = scarlett2_update_input_phantom(mixer);
	if (err < 0)
		return err;

	scarlett2_phantom_update_access(mixer);

	return 0;
}

static int scarlett2_phantom_ctl_get(struct snd_kcontrol *kctl,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	err = scarlett2_check_input_phantom_updated(mixer);
	if (err < 0)
		goto unlock;

	ucontrol->value.integer.value[0] = scarlett2_decode_muteable(
		private->phantom_switch[elem->control]);

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_phantom_ctl_put(struct snd_kcontrol *kctl,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;

	int index = elem->control;
	int oval, val, err;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	err = scarlett2_check_put_during_autogain(mixer);
	if (err < 0)
		goto unlock;

	oval = private->phantom_switch[index];
	val = !!ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->phantom_switch[index] = val;

	/* To set the Gen 4 muteable controls, bit 1 gets set */
	if (private->config_set->items[SCARLETT2_CONFIG_PHANTOM_SWITCH].mute)
		val = (!val) | 0x02;

	/* Send switch change to the device */
	err = scarlett2_usb_set_config(mixer, SCARLETT2_CONFIG_PHANTOM_SWITCH,
				       index + info->phantom_first, val);
	if (err == 0)
		err = 1;

	scarlett2_phantom_update_access(mixer);
	scarlett2_phantom_notify_access(mixer);

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_phantom_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = scarlett2_autogain_disables_ctl_info,
	.get  = scarlett2_phantom_ctl_get,
	.put  = scarlett2_phantom_ctl_put,
};

/*** Phantom Persistence Control ***/

static int scarlett2_phantom_persistence_ctl_get(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct scarlett2_data *private = elem->head.mixer->private_data;

	ucontrol->value.integer.value[0] = private->phantom_persistence;
	return 0;
}

static int scarlett2_phantom_persistence_ctl_put(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;

	int index = elem->control;
	int oval, val, err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	oval = private->phantom_persistence;
	val = !!ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->phantom_persistence = val;

	/* Send switch change to the device */
	err = scarlett2_usb_set_config(
		mixer, SCARLETT2_CONFIG_PHANTOM_PERSISTENCE, index, val);
	if (err == 0)
		err = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_phantom_persistence_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = snd_ctl_boolean_mono_info,
	.get  = scarlett2_phantom_persistence_ctl_get,
	.put  = scarlett2_phantom_persistence_ctl_put,
};

/*** Speaker Switching Control ***/

static int scarlett2_update_monitor_other(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int err;

	/* monitor_other_enable[0] enables speaker switching
	 * monitor_other_enable[1] enables talkback
	 */
	u8 monitor_other_enable[2];

	/* monitor_other_switch[0] activates the alternate speakers
	 * monitor_other_switch[1] activates talkback
	 */
	u8 monitor_other_switch[2];

	private->monitor_other_updated = 0;

	/* if it doesn't do speaker switching then it also doesn't do
	 * talkback
	 */
	if (!info->has_speaker_switching)
		return 0;

	err = scarlett2_usb_get_config(
		mixer, SCARLETT2_CONFIG_MONITOR_OTHER_ENABLE,
		2, monitor_other_enable);
	if (err < 0)
		return err;

	err = scarlett2_usb_get_config(
		mixer, SCARLETT2_CONFIG_MONITOR_OTHER_SWITCH,
		2, monitor_other_switch);
	if (err < 0)
		return err;

	if (!monitor_other_enable[0])
		private->speaker_switching_switch = 0;
	else
		private->speaker_switching_switch = monitor_other_switch[0] + 1;

	if (info->has_talkback) {
		u16 bitmap;
		int i;

		if (!monitor_other_enable[1])
			private->talkback_switch = 0;
		else
			private->talkback_switch = monitor_other_switch[1] + 1;

		err = scarlett2_usb_get_config(mixer,
					       SCARLETT2_CONFIG_TALKBACK_MAP,
					       1, &bitmap);
		if (err < 0)
			return err;
		for (i = 0; i < private->num_mix_out; i++, bitmap >>= 1)
			private->talkback_map[i] = bitmap & 1;
	}

	return 0;
}

static int scarlett2_speaker_switch_enum_ctl_info(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_info *uinfo)
{
	static const char *const values[3] = {
		"Off", "Main", "Alt"
	};

	return snd_ctl_enum_info(uinfo, 1, 3, values);
}

static int scarlett2_speaker_switch_enum_ctl_get(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	if (private->monitor_other_updated) {
		err = scarlett2_update_monitor_other(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.enumerated.item[0] = private->speaker_switching_switch;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

/* when speaker switching gets enabled, switch the main/alt speakers
 * to HW volume and disable those controls
 */
static int scarlett2_speaker_switch_enable(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;
	int i, err;

	for (i = 0; i < 4; i++) {
		int index = line_out_remap(private, i);

		/* switch the main/alt speakers to HW volume */
		if (!private->vol_sw_hw_switch[index]) {
			err = scarlett2_sw_hw_change(private->mixer, i, 1);
			if (err < 0)
				return err;
		}

		/* disable the line out SW/HW switch */
		scarlett2_sw_hw_ctl_ro(private, i);
		snd_ctl_notify(card,
			       SNDRV_CTL_EVENT_MASK_VALUE |
				 SNDRV_CTL_EVENT_MASK_INFO,
			       &private->sw_hw_ctls[i]->id);
	}

	/* when the next monitor-other notify comes in, update the mux
	 * configuration
	 */
	private->speaker_switching_switched = 1;

	return 0;
}

/* when speaker switching gets disabled, reenable the hw/sw controls
 * and invalidate the routing
 */
static void scarlett2_speaker_switch_disable(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;
	int i;

	/* enable the line out SW/HW switch */
	for (i = 0; i < 4; i++) {
		scarlett2_sw_hw_ctl_rw(private, i);
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_INFO,
			       &private->sw_hw_ctls[i]->id);
	}

	/* when the next monitor-other notify comes in, update the mux
	 * configuration
	 */
	private->speaker_switching_switched = 1;
}

static int scarlett2_speaker_switch_enum_ctl_put(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;

	int oval, val, err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	oval = private->speaker_switching_switch;
	val = min(ucontrol->value.enumerated.item[0], 2U);

	if (oval == val)
		goto unlock;

	private->speaker_switching_switch = val;

	/* enable/disable speaker switching */
	err = scarlett2_usb_set_config(
		mixer, SCARLETT2_CONFIG_MONITOR_OTHER_ENABLE,
		0, !!val);
	if (err < 0)
		goto unlock;

	/* if speaker switching is enabled, select main or alt */
	err = scarlett2_usb_set_config(
		mixer, SCARLETT2_CONFIG_MONITOR_OTHER_SWITCH,
		0, val == 2);
	if (err < 0)
		goto unlock;

	/* update controls if speaker switching gets enabled or disabled */
	if (!oval && val)
		err = scarlett2_speaker_switch_enable(mixer);
	else if (oval && !val)
		scarlett2_speaker_switch_disable(mixer);

	if (err == 0)
		err = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_speaker_switch_enum_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = scarlett2_speaker_switch_enum_ctl_info,
	.get  = scarlett2_speaker_switch_enum_ctl_get,
	.put  = scarlett2_speaker_switch_enum_ctl_put,
};

static int scarlett2_add_speaker_switch_ctl(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;

	if (!info->has_speaker_switching)
		return 0;

	return scarlett2_add_new_ctl(
		mixer, &scarlett2_speaker_switch_enum_ctl,
		0, 1, "Speaker Switching Playback Enum",
		&private->speaker_switching_ctl);
}

/*** Talkback and Talkback Map Controls ***/

static int scarlett2_talkback_enum_ctl_info(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_info *uinfo)
{
	static const char *const values[3] = {
		"Disabled", "Off", "On"
	};

	return snd_ctl_enum_info(uinfo, 1, 3, values);
}

static int scarlett2_talkback_enum_ctl_get(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	if (private->monitor_other_updated) {
		err = scarlett2_update_monitor_other(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.enumerated.item[0] = private->talkback_switch;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_talkback_enum_ctl_put(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;

	int oval, val, err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	oval = private->talkback_switch;
	val = min(ucontrol->value.enumerated.item[0], 2U);

	if (oval == val)
		goto unlock;

	private->talkback_switch = val;

	/* enable/disable talkback */
	err = scarlett2_usb_set_config(
		mixer, SCARLETT2_CONFIG_MONITOR_OTHER_ENABLE,
		1, !!val);
	if (err < 0)
		goto unlock;

	/* if talkback is enabled, select main or alt */
	err = scarlett2_usb_set_config(
		mixer, SCARLETT2_CONFIG_MONITOR_OTHER_SWITCH,
		1, val == 2);
	if (err == 0)
		err = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_talkback_enum_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = scarlett2_talkback_enum_ctl_info,
	.get  = scarlett2_talkback_enum_ctl_get,
	.put  = scarlett2_talkback_enum_ctl_put,
};

static int scarlett2_talkback_map_ctl_get(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int index = elem->control;

	ucontrol->value.integer.value[0] = private->talkback_map[index];

	return 0;
}

static int scarlett2_talkback_map_ctl_put(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int index = elem->control;
	int oval, val, err = 0, i;
	u16 bitmap = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	oval = private->talkback_map[index];
	val = !!ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->talkback_map[index] = val;

	for (i = 0; i < private->num_mix_out; i++)
		bitmap |= private->talkback_map[i] << i;

	/* Send updated bitmap to the device */
	err = scarlett2_usb_set_config(mixer, SCARLETT2_CONFIG_TALKBACK_MAP,
				       0, bitmap);
	if (err == 0)
		err = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_talkback_map_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = snd_ctl_boolean_mono_info,
	.get  = scarlett2_talkback_map_ctl_get,
	.put  = scarlett2_talkback_map_ctl_put,
};

static int scarlett2_add_talkback_ctls(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int err, i;
	char s[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];

	if (!info->has_talkback)
		return 0;

	err = scarlett2_add_new_ctl(
		mixer, &scarlett2_talkback_enum_ctl,
		0, 1, "Talkback Playback Enum",
		&private->talkback_ctl);
	if (err < 0)
		return err;

	for (i = 0; i < private->num_mix_out; i++) {
		snprintf(s, sizeof(s),
			 "Talkback Mix %c Playback Switch", i + 'A');
		err = scarlett2_add_new_ctl(mixer, &scarlett2_talkback_map_ctl,
					    i, 1, s, NULL);
		if (err < 0)
			return err;
	}

	return 0;
}

/*** Dim/Mute Controls ***/

static int scarlett2_dim_mute_ctl_get(struct snd_kcontrol *kctl,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	if (private->dim_mute_updated) {
		err = scarlett2_update_dim_mute(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.integer.value[0] = private->dim_mute[elem->control];

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_dim_mute_ctl_put(struct snd_kcontrol *kctl,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int index = elem->control;
	int oval, val, err = 0, i;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	oval = private->dim_mute[index];
	val = !!ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->dim_mute[index] = val;

	/* Send switch change to the device */
	err = scarlett2_usb_set_config(mixer, SCARLETT2_CONFIG_DIM_MUTE,
				       index, val);
	if (err == 0)
		err = 1;

	if (index == SCARLETT2_BUTTON_MUTE)
		for (i = 0; i < private->num_line_out; i++) {
			int line_index = line_out_remap(private, i);

			if (private->vol_sw_hw_switch[line_index]) {
				private->mute_switch[line_index] = val;
				snd_ctl_notify(mixer->chip->card,
					       SNDRV_CTL_EVENT_MASK_VALUE,
					       &private->mute_ctls[i]->id);
			}
		}

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_dim_mute_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = snd_ctl_boolean_mono_info,
	.get  = scarlett2_dim_mute_ctl_get,
	.put  = scarlett2_dim_mute_ctl_put
};

/*** Create the analogue output controls ***/

static int scarlett2_add_line_out_ctls(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int err, i;
	char s[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];

	/* Add R/O HW volume control */
	if (scarlett2_has_config_item(private,
				      SCARLETT2_CONFIG_MASTER_VOLUME)) {
		snprintf(s, sizeof(s), "Master HW Playback Volume");
		err = scarlett2_add_new_ctl(mixer,
					    &scarlett2_master_volume_ctl,
					    0, 1, s, &private->master_vol_ctl);
		if (err < 0)
			return err;
	}

	/* Add R/O headphone volume control */
	if (scarlett2_has_config_item(private,
				      SCARLETT2_CONFIG_HEADPHONE_VOLUME)) {
		snprintf(s, sizeof(s), "Headphone Playback Volume");
		err = scarlett2_add_new_ctl(mixer,
					    &scarlett2_headphone_volume_ctl,
					    0, 1, s,
					    &private->headphone_vol_ctl);
		if (err < 0)
			return err;
	}

	/* Remaining controls are only applicable if the device
	 * has per-channel line-out volume controls.
	 */
	if (!scarlett2_has_config_item(private,
				       SCARLETT2_CONFIG_LINE_OUT_VOLUME))
		return 0;

	/* Add volume controls */
	for (i = 0; i < private->num_line_out; i++) {
		int index = line_out_remap(private, i);

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

		/* Mute Switch */
		snprintf(s, sizeof(s),
			 "Line %02d Mute Playback Switch",
			 i + 1);
		err = scarlett2_add_new_ctl(mixer,
					    &scarlett2_mute_ctl,
					    i, 1, s,
					    &private->mute_ctls[i]);
		if (err < 0)
			return err;

		/* SW/HW Switch */
		if (scarlett2_has_config_item(private,
					      SCARLETT2_CONFIG_SW_HW_SWITCH)) {

			/* Make the fader and mute controls read-only if the
			 * SW/HW switch is set to HW
			 */
			if (private->vol_sw_hw_switch[index])
				scarlett2_vol_ctl_set_writable(mixer, i, 0);

			scnprintf(s, sizeof(s),
				  "Line Out %02d Volume Control Playback Enum",
				  i + 1);
			err = scarlett2_add_new_ctl(mixer,
						    &scarlett2_sw_hw_enum_ctl,
						    i, 1, s,
						    &private->sw_hw_ctls[i]);
			if (err < 0)
				return err;

			/* Make the switch read-only if the line is
			 * involved in speaker switching
			 */
			if (private->speaker_switching_switch && i < 4)
				scarlett2_sw_hw_ctl_ro(private, i);
		}
	}

	/* Add dim/mute controls */
	if (scarlett2_has_config_item(private, SCARLETT2_CONFIG_DIM_MUTE))
		for (i = 0; i < SCARLETT2_DIM_MUTE_COUNT; i++) {
			err = scarlett2_add_new_ctl(
				mixer, &scarlett2_dim_mute_ctl,
				i, 1, scarlett2_dim_mute_names[i],
				&private->dim_mute_ctls[i]);
			if (err < 0)
				return err;
		}

	return 0;
}

/*** Create the analogue input controls ***/

static int scarlett2_add_line_in_ctls(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int err, i;
	char s[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	const char *fmt = "Line In %d %s Capture %s";
	const char *fmt2 = "Line In %d-%d %s Capture %s";

	/* Add input level (line/inst) controls */
	for (i = 0; i < info->level_input_count; i++) {
		scnprintf(s, sizeof(s), fmt, i + 1 + info->level_input_first,
			  "Level", "Enum");
		err = scarlett2_add_new_ctl(mixer, &scarlett2_level_enum_ctl,
					    i, 1, s, &private->level_ctls[i]);
		if (err < 0)
			return err;
	}

	/* Add input pad controls */
	for (i = 0; i < info->pad_input_count; i++) {
		scnprintf(s, sizeof(s), fmt, i + 1, "Pad", "Switch");
		err = scarlett2_add_new_ctl(mixer, &scarlett2_pad_ctl,
					    i, 1, s, &private->pad_ctls[i]);
		if (err < 0)
			return err;
	}

	/* Add input air controls */
	for (i = 0; i < info->air_input_count; i++) {
		scnprintf(s, sizeof(s), fmt, i + 1 + info->air_input_first,
			  "Air", info->air_option ? "Enum" : "Switch");
		err = scarlett2_add_new_ctl(
			mixer, &scarlett2_air_ctl[info->air_option],
			i, 1, s, &private->air_ctls[i]);
		if (err < 0)
			return err;
	}

	/* Add input phantom controls */
	if (info->inputs_per_phantom == 1) {
		for (i = 0; i < info->phantom_count; i++) {
			scnprintf(s, sizeof(s), fmt,
				  i + 1 + info->phantom_first,
				  "Phantom Power", "Switch");
			err = scarlett2_add_new_ctl(
				mixer, &scarlett2_phantom_ctl,
				i, 1, s, &private->phantom_ctls[i]);
			if (err < 0)
				return err;
		}
	} else if (info->inputs_per_phantom > 1) {
		for (i = 0; i < info->phantom_count; i++) {
			int from = i * info->inputs_per_phantom + 1;
			int to = (i + 1) * info->inputs_per_phantom;

			scnprintf(s, sizeof(s), fmt2, from, to,
				  "Phantom Power", "Switch");
			err = scarlett2_add_new_ctl(
				mixer, &scarlett2_phantom_ctl,
				i, 1, s, &private->phantom_ctls[i]);
			if (err < 0)
				return err;
		}
	}
	if (info->phantom_count &&
	    scarlett2_has_config_item(private,
				      SCARLETT2_CONFIG_PHANTOM_PERSISTENCE)) {
		err = scarlett2_add_new_ctl(
			mixer, &scarlett2_phantom_persistence_ctl, 0, 1,
			"Phantom Power Persistence Capture Switch", NULL);
		if (err < 0)
			return err;
	}

	/* Add software-controllable input gain controls */
	if (info->gain_input_count) {
		err = scarlett2_add_new_ctl(
			mixer, &scarlett2_input_select_ctl, 0, 1,
			"Input Select Capture Enum",
			&private->input_select_ctl);
		if (err < 0)
			return err;

		for (i = 0; i < info->gain_input_count; i++) {
			if (i % 2) {
				scnprintf(s, sizeof(s),
					  "Line In %d-%d Link Capture Switch",
					  i, i + 1);
				err = scarlett2_add_new_ctl(
					mixer, &scarlett2_input_link_ctl,
					i / 2, 1, s,
					&private->input_link_ctls[i / 2]);
				if (err < 0)
					return err;
			}

			scnprintf(s, sizeof(s), fmt, i + 1,
				  "Gain", "Volume");
			err = scarlett2_add_new_ctl(
				mixer, &scarlett2_input_gain_ctl,
				i, 1, s, &private->input_gain_ctls[i]);
			if (err < 0)
				return err;

			scnprintf(s, sizeof(s), fmt, i + 1,
				  "Autogain", "Switch");
			err = scarlett2_add_new_ctl(
				mixer, &scarlett2_autogain_switch_ctl,
				i, 1, s, &private->autogain_ctls[i]);
			if (err < 0)
				return err;

			scnprintf(s, sizeof(s), fmt, i + 1,
				  "Autogain Status", "Enum");
			err = scarlett2_add_new_ctl(
				mixer, &scarlett2_autogain_status_ctl,
				i, 1, s, &private->autogain_status_ctls[i]);

			scnprintf(s, sizeof(s), fmt, i + 1,
				  "Safe", "Switch");
			err = scarlett2_add_new_ctl(
				mixer, &scarlett2_safe_ctl,
				i, 1, s, &private->safe_ctls[i]);
			if (err < 0)
				return err;
		}
	}

	/* Add PCM Input Switch control */
	if (scarlett2_has_config_item(private,
				      SCARLETT2_CONFIG_PCM_INPUT_SWITCH)) {
		err = scarlett2_add_new_ctl(
			mixer, &scarlett2_pcm_input_switch_ctl, 0, 1,
			"PCM Input Capture Switch",
			&private->pcm_input_switch_ctl);
		if (err < 0)
			return err;
	}

	return 0;
}

/*** Mixer Volume Controls ***/

static int scarlett2_update_mix(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	int i, err;

	private->mix_updated = 0;

	for (i = 0; i < private->num_mix_out; i++) {
		err = scarlett2_usb_get_mix(mixer, i);
		if (err < 0)
			return err;
	}

	return 1;
}

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
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	if (private->mix_updated) {
		err = scarlett2_update_mix(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.integer.value[0] = private->mix[elem->control];

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_mixer_ctl_put(struct snd_kcontrol *kctl,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int oval, val, mix_num, err = 0;
	int index = elem->control;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	oval = private->mix[index];
	val = clamp(ucontrol->value.integer.value[0],
		    0L, (long)SCARLETT2_MIXER_MAX_VALUE);
	mix_num = index / private->num_mix_in;

	if (oval == val)
		goto unlock;

	private->mix[index] = val;
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
	struct scarlett2_data *private = mixer->private_data;
	int err, i, j;
	int index;
	char s[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];

	for (i = 0, index = 0; i < private->num_mix_out; i++)
		for (j = 0; j < private->num_mix_in; j++, index++) {
			snprintf(s, sizeof(s),
				 "Mix %c Input %02d Playback Volume",
				 'A' + i, j + 1);
			err = scarlett2_add_new_ctl(mixer, &scarlett2_mixer_ctl,
						    index, 1, s,
						    &private->mix_ctls[index]);
			if (err < 0)
				return err;
		}

	return 0;
}

/*** Direct Monitor Control ***/

static int scarlett2_update_direct_monitor(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;

	private->direct_monitor_updated = 0;

	if (!private->info->direct_monitor)
		return 0;

	return scarlett2_usb_get_config(
		mixer, SCARLETT2_CONFIG_DIRECT_MONITOR,
		1, &private->direct_monitor_switch);
}

static int scarlett2_update_monitor_mix(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	int err, i;
	u16 mix_values[SCARLETT2_MONITOR_MIX_MAX];

	if (!private->num_monitor_mix_ctls)
		return 0;

	err = scarlett2_usb_get_config(
		mixer, SCARLETT2_CONFIG_DIRECT_MONITOR_GAIN,
		private->num_monitor_mix_ctls, mix_values);
	if (err < 0)
		return err;

	for (i = 0; i < private->num_monitor_mix_ctls; i++)
		private->monitor_mix[i] = scarlett2_mixer_value_to_db(
			mix_values[i]);

	return 0;
}

static int scarlett2_direct_monitor_ctl_get(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	if (private->direct_monitor_updated) {
		err = scarlett2_update_direct_monitor(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.enumerated.item[0] = private->direct_monitor_switch;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_direct_monitor_ctl_put(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;

	int index = elem->control;
	int oval, val, err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	oval = private->direct_monitor_switch;
	val = min(ucontrol->value.enumerated.item[0], 2U);

	if (oval == val)
		goto unlock;

	private->direct_monitor_switch = val;

	/* Send switch change to the device */
	err = scarlett2_usb_set_config(
		mixer, SCARLETT2_CONFIG_DIRECT_MONITOR, index, val);
	if (err == 0)
		err = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_direct_monitor_stereo_enum_ctl_info(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_info *uinfo)
{
	static const char *const values[3] = {
		"Off", "Mono", "Stereo"
	};

	return snd_ctl_enum_info(uinfo, 1, 3, values);
}

/* Direct Monitor for Solo is mono-only and only needs a boolean control
 * Direct Monitor for 2i2 is selectable between Off/Mono/Stereo
 */
static const struct snd_kcontrol_new scarlett2_direct_monitor_ctl[2] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "",
		.info = snd_ctl_boolean_mono_info,
		.get  = scarlett2_direct_monitor_ctl_get,
		.put  = scarlett2_direct_monitor_ctl_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "",
		.info = scarlett2_direct_monitor_stereo_enum_ctl_info,
		.get  = scarlett2_direct_monitor_ctl_get,
		.put  = scarlett2_direct_monitor_ctl_put,
	}
};

static int scarlett2_monitor_mix_ctl_get(struct snd_kcontrol *kctl,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct scarlett2_data *private = elem->head.mixer->private_data;

	ucontrol->value.integer.value[0] = private->monitor_mix[elem->control];

	return 0;
}

static int scarlett2_monitor_mix_ctl_put(struct snd_kcontrol *kctl,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int oval, val, err = 0;
	int index = elem->control;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	oval = private->monitor_mix[index];
	val = clamp(ucontrol->value.integer.value[0],
		    0L, (long)SCARLETT2_MIXER_MAX_VALUE);

	if (oval == val)
		goto unlock;

	private->monitor_mix[index] = val;
	err = scarlett2_usb_set_config(
		mixer, SCARLETT2_CONFIG_DIRECT_MONITOR_GAIN,
		index, scarlett2_mixer_values[val]);
	if (err == 0)
		err = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_monitor_mix_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
		  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.name = "",
	.info = scarlett2_mixer_ctl_info,
	.get  = scarlett2_monitor_mix_ctl_get,
	.put  = scarlett2_monitor_mix_ctl_put,
	.private_value = SCARLETT2_MIXER_MAX_DB, /* max value */
	.tlv = { .p = db_scale_scarlett2_mixer }
};

static int scarlett2_add_direct_monitor_ctls(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	const char *s;
	int err, i, j, k, index;

	if (!info->direct_monitor)
		return 0;

	s = info->direct_monitor == 1
	      ? "Direct Monitor Playback Switch"
	      : "Direct Monitor Playback Enum";

	err = scarlett2_add_new_ctl(
		mixer, &scarlett2_direct_monitor_ctl[info->direct_monitor - 1],
		0, 1, s, &private->direct_monitor_ctl);
	if (err < 0)
		return err;

	if (!private->num_monitor_mix_ctls)
		return 0;

	/* 1 or 2 direct monitor selections (Mono & Stereo) */
	for (i = 0, index = 0; i < info->direct_monitor; i++) {
		const char * const format =
			"Monitor %sMix %c Input %02d Playback Volume";
		const char *mix_type;

		if (info->direct_monitor == 1)
			mix_type = "";
		else if (i == 0)
			mix_type = "1 ";
		else
			mix_type = "2 ";

		/* 2 Mix outputs, A/Left & B/Right */
		for (j = 0; j < 2; j++)

			/* Mix inputs */
			for (k = 0; k < private->num_mix_in; k++, index++) {
				char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];

				scnprintf(name, sizeof(name), format,
					  mix_type, 'A' + j, k + 1);

				err = scarlett2_add_new_ctl(
					mixer, &scarlett2_monitor_mix_ctl,
					index, 1, name, NULL);
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
	struct scarlett2_data *private = elem->head.mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	const int (*port_count)[SCARLETT2_PORT_DIRNS] = info->port_count;
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
		if (item < port_count[port_type][SCARLETT2_PORT_IN]) {
			const struct scarlett2_port *port =
				&scarlett2_ports[port_type];

			if (port_type == SCARLETT2_PORT_TYPE_MIX &&
			    item >= private->num_mix_out)
				sprintf(uinfo->value.enumerated.name,
					port->dsp_src_descr,
					item - private->num_mix_out + 1);
			else
				sprintf(uinfo->value.enumerated.name,
					port->src_descr,
					item + port->src_num_offset);

			return 0;
		}
		item -= port_count[port_type][SCARLETT2_PORT_IN];
	}

	return -EINVAL;
}

static int scarlett2_mux_src_enum_ctl_get(struct snd_kcontrol *kctl,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int index = line_out_remap(private, elem->control);
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	if (private->mux_updated) {
		err = scarlett2_usb_get_mux(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.enumerated.item[0] = private->mux[index];

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_mux_src_enum_ctl_put(struct snd_kcontrol *kctl,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int index = line_out_remap(private, elem->control);
	int oval, val, err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	oval = private->mux[index];
	val = min(ucontrol->value.enumerated.item[0],
		  private->num_mux_srcs - 1U);

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
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	const int (*port_count)[SCARLETT2_PORT_DIRNS] = info->port_count;
	int port_type, channel, i;

	for (i = 0, port_type = 0;
	     port_type < SCARLETT2_PORT_TYPE_COUNT;
	     port_type++) {
		for (channel = 0;
		     channel < port_count[port_type][SCARLETT2_PORT_OUT];
		     channel++, i++) {
			int err;
			char s[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
			int channel_num = channel + 1;
			const struct scarlett2_port *port =
				&scarlett2_ports[port_type];
			const char *descr = port->dst_descr;

			if (port_type == SCARLETT2_PORT_TYPE_MIX &&
			    channel >= private->num_mix_in) {
				channel_num -= private->num_mix_in;
				descr = port->dsp_dst_descr;
			}

			snprintf(s, sizeof(s) - 5, descr, channel_num);
			strcat(s, " Enum");

			err = scarlett2_add_new_ctl(mixer,
						    &scarlett2_mux_src_enum_ctl,
						    i, 1, s,
						    &private->mux_ctls[i]);
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
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	u8 *meter_level_map = private->meter_level_map;
	u16 meter_levels[SCARLETT2_MAX_METERS];
	int i, err;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	err = scarlett2_usb_get_meter_levels(mixer, elem->channels,
					     meter_levels);
	if (err < 0)
		goto unlock;

	/* copy & translate from meter_levels[] using meter_level_map[] */
	for (i = 0; i < elem->channels; i++) {
		int idx = meter_level_map[i];
		int value;

		if (idx == 255)
			value = 0;
		else
			value = meter_levels[idx];

		ucontrol->value.integer.value[i] = value;
	}

unlock:
	mutex_unlock(&private->data_mutex);

	return err;
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
	struct scarlett2_data *private = mixer->private_data;

	/* devices without a mixer also don't support reporting levels */
	if (!scarlett2_has_mixer(private))
		return 0;

	return scarlett2_add_new_ctl(mixer, &scarlett2_meter_ctl,
				     0, private->num_mux_dsts,
				     "Level Meter", NULL);
}

/*** MSD Controls ***/

static int scarlett2_msd_ctl_get(struct snd_kcontrol *kctl,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct scarlett2_data *private = elem->head.mixer->private_data;

	ucontrol->value.integer.value[0] = private->msd_switch;
	return 0;
}

static int scarlett2_msd_ctl_put(struct snd_kcontrol *kctl,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;

	int oval, val, err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	oval = private->msd_switch;
	val = !!ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->msd_switch = val;

	/* Send switch change to the device */
	err = scarlett2_usb_set_config(mixer, SCARLETT2_CONFIG_MSD_SWITCH,
				       0, val);
	if (err == 0)
		err = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_msd_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = snd_ctl_boolean_mono_info,
	.get  = scarlett2_msd_ctl_get,
	.put  = scarlett2_msd_ctl_put,
};

static int scarlett2_add_msd_ctl(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;

	if (!scarlett2_has_config_item(private, SCARLETT2_CONFIG_MSD_SWITCH))
		return 0;

	/* If MSD mode is off, hide the switch by default */
	if (!private->msd_switch && !(mixer->chip->setup & SCARLETT2_MSD_ENABLE))
		return 0;

	/* Add MSD control */
	return scarlett2_add_new_ctl(mixer, &scarlett2_msd_ctl,
				     0, 1, "MSD Mode Switch", NULL);
}

/*** Standalone Control ***/

static int scarlett2_standalone_ctl_get(struct snd_kcontrol *kctl,
					struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct scarlett2_data *private = elem->head.mixer->private_data;

	ucontrol->value.integer.value[0] = private->standalone_switch;
	return 0;
}

static int scarlett2_standalone_ctl_put(struct snd_kcontrol *kctl,
					struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;

	int oval, val, err = 0;

	mutex_lock(&private->data_mutex);

	if (private->hwdep_in_use) {
		err = -EBUSY;
		goto unlock;
	}

	oval = private->standalone_switch;
	val = !!ucontrol->value.integer.value[0];

	if (oval == val)
		goto unlock;

	private->standalone_switch = val;

	/* Send switch change to the device */
	err = scarlett2_usb_set_config(mixer,
				       SCARLETT2_CONFIG_STANDALONE_SWITCH,
				       0, val);
	if (err == 0)
		err = 1;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static const struct snd_kcontrol_new scarlett2_standalone_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = snd_ctl_boolean_mono_info,
	.get  = scarlett2_standalone_ctl_get,
	.put  = scarlett2_standalone_ctl_put,
};

static int scarlett2_add_standalone_ctl(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;

	if (!scarlett2_has_config_item(private,
				       SCARLETT2_CONFIG_STANDALONE_SWITCH))
		return 0;

	/* Add standalone control */
	return scarlett2_add_new_ctl(mixer, &scarlett2_standalone_ctl,
				     0, 1, "Standalone Switch", NULL);
}

/*** Power Status ***/

static int scarlett2_update_power_status(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	int err;
	u8 power_ext, power_low;

	private->power_status_updated = 0;

	err = scarlett2_usb_get_config(mixer, SCARLETT2_CONFIG_POWER_EXT,
				       1, &power_ext);
	if (err < 0)
		return err;

	err = scarlett2_usb_get_config(mixer, SCARLETT2_CONFIG_POWER_LOW,
				       1, &power_low);
	if (err < 0)
		return err;

	if (power_low)
		private->power_status = SCARLETT2_POWER_STATUS_FAIL;
	else if (power_ext)
		private->power_status = SCARLETT2_POWER_STATUS_EXT;
	else
		private->power_status = SCARLETT2_POWER_STATUS_BUS;

	return 0;
}

static int scarlett2_power_status_ctl_get(struct snd_kcontrol *kctl,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct scarlett2_data *private = mixer->private_data;
	int err = 0;

	mutex_lock(&private->data_mutex);

	if (private->power_status_updated) {
		err = scarlett2_update_power_status(mixer);
		if (err < 0)
			goto unlock;
	}
	ucontrol->value.integer.value[0] = private->power_status;

unlock:
	mutex_unlock(&private->data_mutex);
	return err;
}

static int scarlett2_power_status_ctl_info(
	struct snd_kcontrol *kctl, struct snd_ctl_elem_info *uinfo)
{
	static const char *const values[3] = {
		"External", "Bus", "Fail"
	};

	return snd_ctl_enum_info(uinfo, 1, 3, values);
}

static const struct snd_kcontrol_new scarlett2_power_status_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.name = "",
	.info = scarlett2_power_status_ctl_info,
	.get  = scarlett2_power_status_ctl_get,
};

static int scarlett2_add_power_status_ctl(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;

	if (!scarlett2_has_config_item(private,
				       SCARLETT2_CONFIG_POWER_EXT))
		return 0;

	/* Add power status control */
	return scarlett2_add_new_ctl(mixer, &scarlett2_power_status_ctl,
				     0, 1, "Power Status Card Enum",
				     &private->power_status_ctl);
}

/*** Cleanup/Suspend Callbacks ***/

static void scarlett2_private_free(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;

	cancel_delayed_work_sync(&private->work);
	kfree(private);
	mixer->private_data = NULL;
}

static void scarlett2_private_suspend(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;

	if (cancel_delayed_work_sync(&private->work))
		scarlett2_config_save(private->mixer);
}

/*** Initialisation ***/

static void scarlett2_count_io(struct scarlett2_data *private)
{
	const struct scarlett2_device_info *info = private->info;
	const int (*port_count)[SCARLETT2_PORT_DIRNS] = info->port_count;
	int port_type, srcs = 0, dsts = 0;

	/* Count the number of mux sources and destinations */
	for (port_type = 0;
	     port_type < SCARLETT2_PORT_TYPE_COUNT;
	     port_type++) {
		srcs += port_count[port_type][SCARLETT2_PORT_IN];
		dsts += port_count[port_type][SCARLETT2_PORT_OUT];
	}

	private->num_mux_srcs = srcs;
	private->num_mux_dsts = dsts;

	/* Mixer inputs are mux outputs and vice versa.
	 * Scarlett Gen 4 DSP I/O uses SCARLETT2_PORT_TYPE_MIX but
	 * doesn't have mixer controls.
	 */
	private->num_mix_in =
		port_count[SCARLETT2_PORT_TYPE_MIX][SCARLETT2_PORT_OUT] -
			info->dsp_count;

	private->num_mix_out =
		port_count[SCARLETT2_PORT_TYPE_MIX][SCARLETT2_PORT_IN] -
			info->dsp_count;

	/* Number of analogue line outputs */
	private->num_line_out =
		port_count[SCARLETT2_PORT_TYPE_ANALOGUE][SCARLETT2_PORT_OUT];

	/* Number of monitor mix controls */
	private->num_monitor_mix_ctls =
		info->direct_monitor * 2 * private->num_mix_in;
}

/* Look through the interface descriptors for the Focusrite Control
 * interface (bInterfaceClass = 255 Vendor Specific Class) and set
 * bInterfaceNumber, bEndpointAddress, wMaxPacketSize, and bInterval
 * in private
 */
static int scarlett2_find_fc_interface(struct usb_device *dev,
				       struct scarlett2_data *private)
{
	struct usb_host_config *config = dev->actconfig;
	int i;

	for (i = 0; i < config->desc.bNumInterfaces; i++) {
		struct usb_interface *intf = config->interface[i];
		struct usb_interface_descriptor *desc =
			&intf->altsetting[0].desc;
		struct usb_endpoint_descriptor *epd;

		if (desc->bInterfaceClass != 255)
			continue;

		epd = get_endpoint(intf->altsetting, 0);
		private->bInterfaceNumber = desc->bInterfaceNumber;
		private->bEndpointAddress = epd->bEndpointAddress &
			USB_ENDPOINT_NUMBER_MASK;
		private->wMaxPacketSize = le16_to_cpu(epd->wMaxPacketSize);
		private->bInterval = epd->bInterval;
		return 0;
	}

	return -EINVAL;
}

/* Initialise private data */
static int scarlett2_init_private(struct usb_mixer_interface *mixer,
				  const struct scarlett2_device_entry *entry)
{
	struct scarlett2_data *private =
		kzalloc(sizeof(struct scarlett2_data), GFP_KERNEL);

	if (!private)
		return -ENOMEM;

	mutex_init(&private->usb_mutex);
	mutex_init(&private->data_mutex);
	INIT_DELAYED_WORK(&private->work, scarlett2_config_save_work);

	mixer->private_data = private;
	mixer->private_free = scarlett2_private_free;
	mixer->private_suspend = scarlett2_private_suspend;

	private->info = entry->info;
	private->config_set = entry->info->config_set;
	private->series_name = entry->series_name;
	scarlett2_count_io(private);
	private->scarlett2_seq = 0;
	private->mixer = mixer;

	return scarlett2_find_fc_interface(mixer->chip->dev, private);
}

/* Cargo cult proprietary initialisation sequence */
static int scarlett2_usb_init(struct usb_mixer_interface *mixer)
{
	struct usb_device *dev = mixer->chip->dev;
	struct scarlett2_data *private = mixer->private_data;
	u8 step0_buf[24];
	u8 step2_buf[84];
	int err;

	if (usb_pipe_type_check(dev, usb_sndctrlpipe(dev, 0)))
		return -EINVAL;

	/* step 0 */
	err = scarlett2_usb_rx(dev, private->bInterfaceNumber,
			       SCARLETT2_USB_CMD_INIT,
			       step0_buf, sizeof(step0_buf));
	if (err < 0)
		return err;

	/* step 1 */
	private->scarlett2_seq = 1;
	err = scarlett2_usb(mixer, SCARLETT2_USB_INIT_1, NULL, 0, NULL, 0);
	if (err < 0)
		return err;

	/* step 2 */
	private->scarlett2_seq = 1;
	err = scarlett2_usb(mixer, SCARLETT2_USB_INIT_2,
			    NULL, 0,
			    step2_buf, sizeof(step2_buf));
	if (err < 0)
		return err;

	/* extract 4-byte firmware version from step2_buf[8] */
	private->firmware_version = le32_to_cpu(*(__le32 *)(step2_buf + 8));
	usb_audio_info(mixer->chip,
		       "Firmware version %d\n",
		       private->firmware_version);

	return 0;
}

/* Get the flash segment numbers for the App_Settings and App_Upgrade
 * segments and put them in the private data
 */
static int scarlett2_get_flash_segment_nums(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	int err, count, i;

	struct {
		__le32 size;
		__le32 count;
		u8 unknown[8];
	} __packed flash_info;

	struct {
		__le32 size;
		__le32 flags;
		char name[16];
	} __packed segment_info;

	err = scarlett2_usb(mixer, SCARLETT2_USB_INFO_FLASH,
			    NULL, 0,
			    &flash_info, sizeof(flash_info));
	if (err < 0)
		return err;

	count = le32_to_cpu(flash_info.count);

	/* sanity check count */
	if (count < SCARLETT2_SEGMENT_NUM_MIN ||
	    count > SCARLETT2_SEGMENT_NUM_MAX + 1) {
		usb_audio_err(mixer->chip,
			      "invalid flash segment count: %d\n", count);
		return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		__le32 segment_num_req = cpu_to_le32(i);
		int flash_segment_id;

		err = scarlett2_usb(mixer, SCARLETT2_USB_INFO_SEGMENT,
				    &segment_num_req, sizeof(segment_num_req),
				    &segment_info, sizeof(segment_info));
		if (err < 0) {
			usb_audio_err(mixer->chip,
				"failed to get flash segment info %d: %d\n",
				i, err);
			return err;
		}

		if (!strncmp(segment_info.name,
			     SCARLETT2_SEGMENT_SETTINGS_NAME, 16))
			flash_segment_id = SCARLETT2_SEGMENT_ID_SETTINGS;
		else if (!strncmp(segment_info.name,
				  SCARLETT2_SEGMENT_FIRMWARE_NAME, 16))
			flash_segment_id = SCARLETT2_SEGMENT_ID_FIRMWARE;
		else
			continue;

		private->flash_segment_nums[flash_segment_id] = i;
		private->flash_segment_blocks[flash_segment_id] =
			le32_to_cpu(segment_info.size) /
				SCARLETT2_FLASH_BLOCK_SIZE;
	}

	/* segment 0 is App_Gold and we never want to touch that, so
	 * use 0 as the "not-found" value
	 */
	if (!private->flash_segment_nums[SCARLETT2_SEGMENT_ID_SETTINGS]) {
		usb_audio_err(mixer->chip,
			      "failed to find flash segment %s\n",
			      SCARLETT2_SEGMENT_SETTINGS_NAME);
		return -EINVAL;
	}
	if (!private->flash_segment_nums[SCARLETT2_SEGMENT_ID_FIRMWARE]) {
		usb_audio_err(mixer->chip,
			      "failed to find flash segment %s\n",
			      SCARLETT2_SEGMENT_FIRMWARE_NAME);
		return -EINVAL;
	}

	return 0;
}

/* Read configuration from the interface on start */
static int scarlett2_read_configs(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int err, i;

	if (scarlett2_has_config_item(private, SCARLETT2_CONFIG_MSD_SWITCH)) {
		err = scarlett2_usb_get_config(
			mixer, SCARLETT2_CONFIG_MSD_SWITCH,
			1, &private->msd_switch);
		if (err < 0)
			return err;
	}

	if (private->firmware_version < info->min_firmware_version) {
		usb_audio_err(mixer->chip,
			      "Focusrite %s firmware version %d is too old; "
			      "need %d",
			      private->series_name,
			      private->firmware_version,
			      info->min_firmware_version);
		return 0;
	}

	/* no other controls are created if MSD mode is on */
	if (private->msd_switch)
		return 0;

	err = scarlett2_update_input_level(mixer);
	if (err < 0)
		return err;

	err = scarlett2_update_input_pad(mixer);
	if (err < 0)
		return err;

	err = scarlett2_update_input_air(mixer);
	if (err < 0)
		return err;

	err = scarlett2_update_input_phantom(mixer);
	if (err < 0)
		return err;

	err = scarlett2_update_direct_monitor(mixer);
	if (err < 0)
		return err;

	/* the rest of the configuration is for devices with a mixer */
	if (!scarlett2_has_mixer(private))
		return 0;

	err = scarlett2_update_monitor_mix(mixer);
	if (err < 0)
		return err;

	err = scarlett2_update_monitor_other(mixer);
	if (err < 0)
		return err;

	if (scarlett2_has_config_item(private,
				      SCARLETT2_CONFIG_STANDALONE_SWITCH)) {
		err = scarlett2_usb_get_config(
			mixer, SCARLETT2_CONFIG_STANDALONE_SWITCH,
			1, &private->standalone_switch);
		if (err < 0)
			return err;
	}

	if (scarlett2_has_config_item(private,
				      SCARLETT2_CONFIG_POWER_EXT)) {
		err = scarlett2_update_power_status(mixer);
		if (err < 0)
			return err;
	}

	err = scarlett2_update_sync(mixer);
	if (err < 0)
		return err;

	if (scarlett2_has_config_item(private,
				      SCARLETT2_CONFIG_LINE_OUT_VOLUME)) {
		s16 sw_vol[SCARLETT2_ANALOGUE_MAX];

		/* read SW line out volume */
		err = scarlett2_usb_get_config(
			mixer, SCARLETT2_CONFIG_LINE_OUT_VOLUME,
			private->num_line_out, &sw_vol);
		if (err < 0)
			return err;

		for (i = 0; i < private->num_line_out; i++)
			private->vol[i] = clamp(
				sw_vol[i] + SCARLETT2_VOLUME_BIAS,
				0, SCARLETT2_VOLUME_BIAS);

		/* read SW mute */
		err = scarlett2_usb_get_config(
			mixer, SCARLETT2_CONFIG_MUTE_SWITCH,
			private->num_line_out, &private->mute_switch);
		if (err < 0)
			return err;

		for (i = 0; i < private->num_line_out; i++)
			private->mute_switch[i] =
				!!private->mute_switch[i];

		/* read SW/HW switches */
		if (scarlett2_has_config_item(private,
					      SCARLETT2_CONFIG_SW_HW_SWITCH)) {
			err = scarlett2_usb_get_config(
				mixer, SCARLETT2_CONFIG_SW_HW_SWITCH,
				private->num_line_out,
				&private->vol_sw_hw_switch);
			if (err < 0)
				return err;

			for (i = 0; i < private->num_line_out; i++)
				private->vol_sw_hw_switch[i] =
					!!private->vol_sw_hw_switch[i];
		}
	}

	err = scarlett2_update_volumes(mixer);
	if (err < 0)
		return err;

	err = scarlett2_update_dim_mute(mixer);
	if (err < 0)
		return err;

	err = scarlett2_update_input_select(mixer);
	if (err < 0)
		return err;

	err = scarlett2_update_input_gain(mixer);
	if (err < 0)
		return err;

	err = scarlett2_update_autogain(mixer);
	if (err < 0)
		return err;

	err = scarlett2_update_input_safe(mixer);
	if (err < 0)
		return err;

	if (scarlett2_has_config_item(private,
				      SCARLETT2_CONFIG_PCM_INPUT_SWITCH)) {
		err = scarlett2_update_pcm_input_switch(mixer);
		if (err < 0)
			return err;
	}

	err = scarlett2_update_mix(mixer);
	if (err < 0)
		return err;

	return scarlett2_usb_get_mux(mixer);
}

/* Notify on sync change */
static void scarlett2_notify_sync(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;

	private->sync_updated = 1;

	snd_ctl_notify(mixer->chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
		       &private->sync_ctl->id);
}

/* Notify on monitor change (Gen 2/3) */
static void scarlett2_notify_monitor(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;
	int i;

	if (!scarlett2_has_config_item(private, SCARLETT2_CONFIG_SW_HW_SWITCH))
		return;

	private->vol_updated = 1;

	snd_ctl_notify(mixer->chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
		       &private->master_vol_ctl->id);

	for (i = 0; i < private->num_line_out; i++)
		if (private->vol_sw_hw_switch[line_out_remap(private, i)])
			snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &private->vol_ctls[i]->id);
}

/* Notify on volume change (Gen 4) */
static void scarlett2_notify_volume(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;

	private->vol_updated = 1;

	snd_ctl_notify(mixer->chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
		       &private->master_vol_ctl->id);
	snd_ctl_notify(mixer->chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
		       &private->headphone_vol_ctl->id);
}

/* Notify on dim/mute change */
static void scarlett2_notify_dim_mute(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;
	int i;

	if (!scarlett2_has_config_item(private, SCARLETT2_CONFIG_SW_HW_SWITCH))
		return;

	private->dim_mute_updated = 1;

	for (i = 0; i < SCARLETT2_DIM_MUTE_COUNT; i++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &private->dim_mute_ctls[i]->id);

	for (i = 0; i < private->num_line_out; i++)
		if (private->vol_sw_hw_switch[line_out_remap(private, i)])
			snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &private->mute_ctls[i]->id);
}

/* Notify on input level switch change */
static void scarlett2_notify_input_level(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int i;

	private->input_level_updated = 1;

	for (i = 0; i < info->level_input_count; i++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &private->level_ctls[i]->id);
}

/* Notify on input pad switch change */
static void scarlett2_notify_input_pad(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int i;

	private->input_pad_updated = 1;

	for (i = 0; i < info->pad_input_count; i++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &private->pad_ctls[i]->id);
}

/* Notify on input air switch change */
static void scarlett2_notify_input_air(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int i;

	private->input_air_updated = 1;

	for (i = 0; i < info->air_input_count; i++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &private->air_ctls[i]->id);
}

/* Notify on input phantom switch change */
static void scarlett2_notify_input_phantom(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int i;

	private->input_phantom_updated = 1;

	for (i = 0; i < info->phantom_count; i++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &private->phantom_ctls[i]->id);

	scarlett2_phantom_notify_access(mixer);
}

/* Notify on "input other" change (level/pad/air/phantom) */
static void scarlett2_notify_input_other(struct usb_mixer_interface *mixer)
{
	scarlett2_notify_input_level(mixer);
	scarlett2_notify_input_pad(mixer);
	scarlett2_notify_input_air(mixer);
	scarlett2_notify_input_phantom(mixer);
}

/* Notify on input select change */
static void scarlett2_notify_input_select(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int i;

	if (!info->gain_input_count)
		return;

	private->input_select_updated = 1;

	snd_ctl_notify(card,
		       SNDRV_CTL_EVENT_MASK_VALUE | SNDRV_CTL_EVENT_MASK_INFO,
		       &private->input_select_ctl->id);

	for (i = 0; i < info->gain_input_count / 2; i++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &private->input_link_ctls[i]->id);
}

/* Notify on input gain change */
static void scarlett2_notify_input_gain(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int i;

	if (!info->gain_input_count)
		return;

	private->input_gain_updated = 1;

	for (i = 0; i < info->gain_input_count; i++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &private->input_gain_ctls[i]->id);
}

/* Notify on autogain change */
static void scarlett2_notify_autogain(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int i;

	if (!info->gain_input_count)
		return;

	private->autogain_updated = 1;

	for (i = 0; i < info->gain_input_count; i++) {
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &private->autogain_ctls[i]->id);
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &private->autogain_status_ctls[i]->id);
	}

	scarlett2_autogain_notify_access(mixer);
}

/* Notify on input safe switch change */
static void scarlett2_notify_input_safe(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;
	int i;

	if (!info->gain_input_count)
		return;

	private->input_safe_updated = 1;

	for (i = 0; i < info->gain_input_count; i++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &private->safe_ctls[i]->id);
}

/* Notify on "monitor other" change (speaker switching, talkback) */
static void scarlett2_notify_monitor_other(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_device_info *info = private->info;

	private->monitor_other_updated = 1;

	if (info->has_speaker_switching)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &private->speaker_switching_ctl->id);

	if (info->has_talkback)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &private->talkback_ctl->id);

	/* if speaker switching was recently enabled or disabled,
	 * invalidate the dim/mute and mux enum controls
	 */
	if (private->speaker_switching_switched) {
		int i;

		scarlett2_notify_dim_mute(mixer);

		private->speaker_switching_switched = 0;
		private->mux_updated = 1;

		for (i = 0; i < private->num_mux_dsts; i++)
			snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &private->mux_ctls[i]->id);
	}
}

/* Notify on direct monitor switch change */
static void scarlett2_notify_direct_monitor(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;
	int count = private->num_mix_in * private->num_mix_out;
	int i;

	private->direct_monitor_updated = 1;

	snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
		       &private->direct_monitor_ctl->id);

	if (!scarlett2_has_mixer(private))
		return;

	private->mix_updated = 1;

	/* Notify of change to the mix controls */
	for (i = 0; i < count; i++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &private->mix_ctls[i]->id);
}

/* Notify on power change */
static void scarlett2_notify_power_status(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;

	private->power_status_updated = 1;

	snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
		       &private->power_status_ctl->id);
}

/* Notify on mux change */
static void scarlett2_notify_mux(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;
	int i;

	private->mux_updated = 1;

	for (i = 0; i < private->num_mux_dsts; i++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &private->mux_ctls[i]->id);
}

/* Notify on PCM input switch change */
static void scarlett2_notify_pcm_input_switch(struct usb_mixer_interface *mixer)
{
	struct snd_card *card = mixer->chip->card;
	struct scarlett2_data *private = mixer->private_data;

	private->pcm_input_switch_updated = 1;

	snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
		       &private->pcm_input_switch_ctl->id);

	scarlett2_notify_mux(mixer);
}

/* Interrupt callback */
static void scarlett2_notify(struct urb *urb)
{
	struct usb_mixer_interface *mixer = urb->context;
	int len = urb->actual_length;
	int ustatus = urb->status;
	u32 data;
	struct scarlett2_data *private = mixer->private_data;
	const struct scarlett2_notification *notifications =
		private->config_set->notifications;

	if (ustatus != 0 || len != 8)
		goto requeue;

	data = le32_to_cpu(*(__le32 *)urb->transfer_buffer);

	while (data && notifications->mask) {
		if (data & notifications->mask) {
			data &= ~notifications->mask;
			if (notifications->func)
				notifications->func(mixer);
		}
		notifications++;
	}

	if (data)
		usb_audio_warn(mixer->chip,
			       "%s: Unhandled notification: 0x%08x\n",
			       __func__, data);

requeue:
	if (ustatus != -ENOENT &&
	    ustatus != -ECONNRESET &&
	    ustatus != -ESHUTDOWN) {
		urb->dev = mixer->chip->dev;
		usb_submit_urb(urb, GFP_ATOMIC);
	}
}

static int scarlett2_init_notify(struct usb_mixer_interface *mixer)
{
	struct usb_device *dev = mixer->chip->dev;
	struct scarlett2_data *private = mixer->private_data;
	unsigned int pipe = usb_rcvintpipe(dev, private->bEndpointAddress);
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

	transfer_buffer = kmalloc(private->wMaxPacketSize, GFP_KERNEL);
	if (!transfer_buffer)
		return -ENOMEM;

	usb_fill_int_urb(mixer->urb, dev, pipe,
			 transfer_buffer, private->wMaxPacketSize,
			 scarlett2_notify, mixer, private->bInterval);

	return usb_submit_urb(mixer->urb, GFP_KERNEL);
}

static const struct scarlett2_device_entry *get_scarlett2_device_entry(
	struct usb_mixer_interface *mixer)
{
	const struct scarlett2_device_entry *entry = scarlett2_devices;

	/* Find entry in scarlett2_devices */
	while (entry->usb_id && entry->usb_id != mixer->chip->usb_id)
		entry++;
	if (!entry->usb_id)
		return NULL;

	return entry;
}

static int snd_scarlett2_controls_create(
	struct usb_mixer_interface *mixer,
	const struct scarlett2_device_entry *entry)
{
	struct scarlett2_data *private;
	int err;

	/* Initialise private data */
	err = scarlett2_init_private(mixer, entry);
	if (err < 0)
		return err;

	private = mixer->private_data;

	/* Send proprietary USB initialisation sequence */
	err = scarlett2_usb_init(mixer);
	if (err < 0)
		return err;

	/* Get the upgrade & settings flash segment numbers */
	err = scarlett2_get_flash_segment_nums(mixer);
	if (err < 0)
		return err;

	/* Add firmware version control */
	err = scarlett2_add_firmware_version_ctl(mixer);
	if (err < 0)
		return err;

	/* Add minimum firmware version control */
	err = scarlett2_add_min_firmware_version_ctl(mixer);
	if (err < 0)
		return err;

	/* Read volume levels and controls from the interface */
	err = scarlett2_read_configs(mixer);
	if (err < 0)
		return err;

	/* Create the MSD control */
	err = scarlett2_add_msd_ctl(mixer);
	if (err < 0)
		return err;

	/* If MSD mode is enabled, or if the firmware version is too
	 * old, don't create any other controls
	 */
	if (private->msd_switch ||
	    private->firmware_version < private->info->min_firmware_version)
		return 0;

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

	/* Create the sync control */
	err = scarlett2_add_sync_ctl(mixer);
	if (err < 0)
		return err;

	/* Create the direct monitor control(s) */
	err = scarlett2_add_direct_monitor_ctls(mixer);
	if (err < 0)
		return err;

	/* Create the speaker switching control */
	err = scarlett2_add_speaker_switch_ctl(mixer);
	if (err < 0)
		return err;

	/* Create the talkback controls */
	err = scarlett2_add_talkback_ctls(mixer);
	if (err < 0)
		return err;

	/* Create the standalone control */
	err = scarlett2_add_standalone_ctl(mixer);
	if (err < 0)
		return err;

	/* Create the power status control */
	err = scarlett2_add_power_status_ctl(mixer);
	if (err < 0)
		return err;

	/* Set the access mode of controls disabled during
	 * autogain/phantom power switching.
	 */
	if (private->info->gain_input_count) {
		scarlett2_autogain_update_access(mixer);
		scarlett2_phantom_update_access(mixer);
	}

	/* Set up the interrupt polling */
	err = scarlett2_init_notify(mixer);
	if (err < 0)
		return err;

	return 0;
}

/*** hwdep interface ***/

/* Set private->hwdep_in_use; prevents access to the ALSA controls
 * while doing a config erase/firmware upgrade.
 */
static void scarlett2_lock(struct scarlett2_data *private)
{
	mutex_lock(&private->data_mutex);
	private->hwdep_in_use = 1;
	mutex_unlock(&private->data_mutex);
}

/* Call SCARLETT2_USB_GET_ERASE to get the erase progress */
static int scarlett2_get_erase_progress(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	int segment_id, segment_num, err;
	u8 erase_resp;

	struct {
		__le32 segment_num;
		__le32 pad;
	} __packed erase_req;

	segment_id = private->selected_flash_segment_id;
	segment_num = private->flash_segment_nums[segment_id];

	if (segment_num < SCARLETT2_SEGMENT_NUM_MIN ||
	    segment_num > SCARLETT2_SEGMENT_NUM_MAX)
		return -EFAULT;

	/* Send the erase progress request */
	erase_req.segment_num = cpu_to_le32(segment_num);
	erase_req.pad = 0;

	err = scarlett2_usb(mixer, SCARLETT2_USB_GET_ERASE,
			    &erase_req, sizeof(erase_req),
			    &erase_resp, sizeof(erase_resp));
	if (err < 0)
		return err;

	return erase_resp;
}

/* Repeatedly call scarlett2_get_erase_progress() until it returns
 * 0xff (erase complete) or we've waited 10 seconds (it usually takes
 * <3 seconds).
 */
static int scarlett2_wait_for_erase(struct usb_mixer_interface *mixer)
{
	int i, err;

	for (i = 0; i < 100; i++) {
		err = scarlett2_get_erase_progress(mixer);
		if (err < 0)
			return err;

		if (err == 0xff)
			return 0;

		msleep(100);
	}

	return -ETIMEDOUT;
}

/* Reboot the device; wait for the erase to complete if one is in
 * progress.
 */
static int scarlett2_reboot(struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;

	if (private->flash_write_state ==
	      SCARLETT2_FLASH_WRITE_STATE_ERASING) {
		int err = scarlett2_wait_for_erase(mixer);

		if (err < 0)
			return err;
	}

	return scarlett2_usb(mixer, SCARLETT2_USB_REBOOT, NULL, 0, NULL, 0);
}

/* Select a flash segment for erasing (and possibly writing to) */
static int scarlett2_ioctl_select_flash_segment(
	struct usb_mixer_interface *mixer,
	unsigned long arg)
{
	struct scarlett2_data *private = mixer->private_data;
	int segment_id, segment_num;

	if (get_user(segment_id, (int __user *)arg))
		return -EFAULT;

	/* Check the segment ID and segment number */
	if (segment_id < 0 || segment_id >= SCARLETT2_SEGMENT_ID_COUNT)
		return -EINVAL;

	segment_num = private->flash_segment_nums[segment_id];
	if (segment_num < SCARLETT2_SEGMENT_NUM_MIN ||
	    segment_num > SCARLETT2_SEGMENT_NUM_MAX) {
		usb_audio_err(mixer->chip,
			      "%s: invalid segment number %d\n",
			      __func__, segment_id);
		return -EFAULT;
	}

	/* If erasing, wait for it to complete */
	if (private->flash_write_state == SCARLETT2_FLASH_WRITE_STATE_ERASING) {
		int err = scarlett2_wait_for_erase(mixer);

		if (err < 0)
			return err;
	}

	/* Save the selected segment ID and set the state to SELECTED */
	private->selected_flash_segment_id = segment_id;
	private->flash_write_state = SCARLETT2_FLASH_WRITE_STATE_SELECTED;

	return 0;
}

/* Erase the previously-selected flash segment */
static int scarlett2_ioctl_erase_flash_segment(
	struct usb_mixer_interface *mixer)
{
	struct scarlett2_data *private = mixer->private_data;
	int segment_id, segment_num, err;

	struct {
		__le32 segment_num;
		__le32 pad;
	} __packed erase_req;

	if (private->flash_write_state != SCARLETT2_FLASH_WRITE_STATE_SELECTED)
		return -EINVAL;

	segment_id = private->selected_flash_segment_id;
	segment_num = private->flash_segment_nums[segment_id];

	if (segment_num < SCARLETT2_SEGMENT_NUM_MIN ||
	    segment_num > SCARLETT2_SEGMENT_NUM_MAX)
		return -EFAULT;

	/* Prevent access to ALSA controls that access the device from
	 * here on
	 */
	scarlett2_lock(private);

	/* Send the erase request */
	erase_req.segment_num = cpu_to_le32(segment_num);
	erase_req.pad = 0;

	err = scarlett2_usb(mixer, SCARLETT2_USB_ERASE_SEGMENT,
			    &erase_req, sizeof(erase_req),
			    NULL, 0);
	if (err < 0)
		return err;

	/* On success, change the state from SELECTED to ERASING */
	private->flash_write_state = SCARLETT2_FLASH_WRITE_STATE_ERASING;

	return 0;
}

/* Get the erase progress from the device */
static int scarlett2_ioctl_get_erase_progress(
	struct usb_mixer_interface *mixer,
	unsigned long arg)
{
	struct scarlett2_data *private = mixer->private_data;
	struct scarlett2_flash_segment_erase_progress progress;
	int segment_id, segment_num, err;
	u8 erase_resp;

	struct {
		__le32 segment_num;
		__le32 pad;
	} __packed erase_req;

	/* Check that we're erasing */
	if (private->flash_write_state != SCARLETT2_FLASH_WRITE_STATE_ERASING)
		return -EINVAL;

	segment_id = private->selected_flash_segment_id;
	segment_num = private->flash_segment_nums[segment_id];

	if (segment_num < SCARLETT2_SEGMENT_NUM_MIN ||
	    segment_num > SCARLETT2_SEGMENT_NUM_MAX)
		return -EFAULT;

	/* Send the erase progress request */
	erase_req.segment_num = cpu_to_le32(segment_num);
	erase_req.pad = 0;

	err = scarlett2_usb(mixer, SCARLETT2_USB_GET_ERASE,
			    &erase_req, sizeof(erase_req),
			    &erase_resp, sizeof(erase_resp));
	if (err < 0)
		return err;

	progress.progress = erase_resp;
	progress.num_blocks = private->flash_segment_blocks[segment_id];

	if (copy_to_user((void __user *)arg, &progress, sizeof(progress)))
		return -EFAULT;

	/* If the erase is complete, change the state from ERASING to
	 * WRITE.
	 */
	if (progress.progress == 0xff)
		private->flash_write_state = SCARLETT2_FLASH_WRITE_STATE_WRITE;

	return 0;
}

static int scarlett2_hwdep_open(struct snd_hwdep *hw, struct file *file)
{
	struct usb_mixer_interface *mixer = hw->private_data;
	struct scarlett2_data *private = mixer->private_data;

	/* If erasing, wait for it to complete */
	if (private->flash_write_state ==
	      SCARLETT2_FLASH_WRITE_STATE_ERASING) {
		int err = scarlett2_wait_for_erase(mixer);

		if (err < 0)
			return err;
	}

	/* Set the state to IDLE */
	private->flash_write_state = SCARLETT2_FLASH_WRITE_STATE_IDLE;

	return 0;
}

static int scarlett2_hwdep_ioctl(struct snd_hwdep *hw, struct file *file,
				 unsigned int cmd, unsigned long arg)
{
	struct usb_mixer_interface *mixer = hw->private_data;

	switch (cmd) {

	case SCARLETT2_IOCTL_PVERSION:
		return put_user(SCARLETT2_HWDEP_VERSION,
				(int __user *)arg) ? -EFAULT : 0;

	case SCARLETT2_IOCTL_REBOOT:
		return scarlett2_reboot(mixer);

	case SCARLETT2_IOCTL_SELECT_FLASH_SEGMENT:
		return scarlett2_ioctl_select_flash_segment(mixer, arg);

	case SCARLETT2_IOCTL_ERASE_FLASH_SEGMENT:
		return scarlett2_ioctl_erase_flash_segment(mixer);

	case SCARLETT2_IOCTL_GET_ERASE_PROGRESS:
		return scarlett2_ioctl_get_erase_progress(mixer, arg);

	default:
		return -ENOIOCTLCMD;
	}
}

static long scarlett2_hwdep_write(struct snd_hwdep *hw,
				  const char __user *buf,
				  long count, loff_t *offset)
{
	struct usb_mixer_interface *mixer = hw->private_data;
	struct scarlett2_data *private = mixer->private_data;
	int segment_id, segment_num, err, len;
	int flash_size;

	/* SCARLETT2_USB_WRITE_SEGMENT request data */
	struct {
		__le32 segment_num;
		__le32 offset;
		__le32 pad;
		u8 data[];
	} __packed *req;

	/* Calculate the maximum permitted in data[] */
	const size_t max_data_size = SCARLETT2_FLASH_WRITE_MAX -
				     offsetof(typeof(*req), data);

	/* If erasing, wait for it to complete */
	if (private->flash_write_state ==
	      SCARLETT2_FLASH_WRITE_STATE_ERASING) {
		err = scarlett2_wait_for_erase(mixer);
		if (err < 0)
			return err;
		private->flash_write_state = SCARLETT2_FLASH_WRITE_STATE_WRITE;

	/* Check that an erase has been done & completed */
	} else if (private->flash_write_state !=
		     SCARLETT2_FLASH_WRITE_STATE_WRITE) {
		return -EINVAL;
	}

	/* Check that we're writing to the upgrade firmware */
	segment_id = private->selected_flash_segment_id;
	if (segment_id != SCARLETT2_SEGMENT_ID_FIRMWARE)
		return -EINVAL;

	segment_num = private->flash_segment_nums[segment_id];
	if (segment_num < SCARLETT2_SEGMENT_NUM_MIN ||
	    segment_num > SCARLETT2_SEGMENT_NUM_MAX)
		return -EFAULT;

	/* Validate the offset and count */
	flash_size = private->flash_segment_blocks[segment_id] *
		     SCARLETT2_FLASH_BLOCK_SIZE;

	if (count < 0 || *offset < 0 || *offset + count >= flash_size)
		return -EINVAL;

	if (!count)
		return 0;

	/* Limit the *req size to SCARLETT2_FLASH_WRITE_MAX */
	if (count > max_data_size)
		count = max_data_size;

	/* Create and send the request */
	len = struct_size(req, data, count);
	req = kzalloc(len, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	req->segment_num = cpu_to_le32(segment_num);
	req->offset = cpu_to_le32(*offset);
	req->pad = 0;

	if (copy_from_user(req->data, buf, count)) {
		err = -EFAULT;
		goto error;
	}

	err = scarlett2_usb(mixer, SCARLETT2_USB_WRITE_SEGMENT,
			    req, len, NULL, 0);
	if (err < 0)
		goto error;

	*offset += count;
	err = count;

error:
	kfree(req);
	return err;
}

static int scarlett2_hwdep_release(struct snd_hwdep *hw, struct file *file)
{
	struct usb_mixer_interface *mixer = hw->private_data;
	struct scarlett2_data *private = mixer->private_data;

	/* Return from the SELECTED or WRITE state to IDLE.
	 * The ERASING state is left as-is, and checked on next open.
	 */
	if (private &&
	    private->hwdep_in_use &&
	    private->flash_write_state != SCARLETT2_FLASH_WRITE_STATE_ERASING)
		private->flash_write_state = SCARLETT2_FLASH_WRITE_STATE_IDLE;

	return 0;
}

static int scarlett2_hwdep_init(struct usb_mixer_interface *mixer)
{
	struct snd_hwdep *hw;
	int err;

	err = snd_hwdep_new(mixer->chip->card, "Focusrite Control", 0, &hw);
	if (err < 0)
		return err;

	hw->private_data = mixer;
	hw->exclusive = 1;
	hw->ops.open = scarlett2_hwdep_open;
	hw->ops.ioctl = scarlett2_hwdep_ioctl;
	hw->ops.write = scarlett2_hwdep_write;
	hw->ops.release = scarlett2_hwdep_release;

	return 0;
}

int snd_scarlett2_init(struct usb_mixer_interface *mixer)
{
	struct snd_usb_audio *chip = mixer->chip;
	const struct scarlett2_device_entry *entry;
	int err;

	/* only use UAC_VERSION_2 */
	if (!mixer->protocol)
		return 0;

	/* find entry in scarlett2_devices */
	entry = get_scarlett2_device_entry(mixer);
	if (!entry) {
		usb_audio_err(mixer->chip,
			      "%s: missing device entry for %04x:%04x\n",
			      __func__,
			      USB_ID_VENDOR(chip->usb_id),
			      USB_ID_PRODUCT(chip->usb_id));
		return 0;
	}

	if (chip->setup & SCARLETT2_DISABLE) {
		usb_audio_info(chip,
			"Focusrite %s Mixer Driver disabled "
			"by modprobe options (snd_usb_audio "
			"vid=0x%04x pid=0x%04x device_setup=%d)\n",
			entry->series_name,
			USB_ID_VENDOR(chip->usb_id),
			USB_ID_PRODUCT(chip->usb_id),
			SCARLETT2_DISABLE);
		return 0;
	}

	usb_audio_info(chip,
		"Focusrite %s Mixer Driver enabled (pid=0x%04x); "
		"report any issues to "
		"https://github.com/geoffreybennett/scarlett-gen2/issues",
		entry->series_name,
		USB_ID_PRODUCT(chip->usb_id));

	err = snd_scarlett2_controls_create(mixer, entry);
	if (err < 0) {
		usb_audio_err(mixer->chip,
			      "Error initialising %s Mixer Driver: %d",
			      entry->series_name,
			      err);
		return err;
	}

	err = scarlett2_hwdep_init(mixer);
	if (err < 0)
		usb_audio_err(mixer->chip,
			      "Error creating %s hwdep device: %d",
			      entry->series_name,
			      err);

	return err;
}
