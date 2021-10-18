/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_SOUND_FIREWIRE_H_INCLUDED
#define _UAPI_SOUND_FIREWIRE_H_INCLUDED

#include <linux/ioctl.h>
#include <linux/types.h>

/* events can be read() from the hwdep device */

#define SNDRV_FIREWIRE_EVENT_LOCK_STATUS	0x000010cc
#define SNDRV_FIREWIRE_EVENT_DICE_NOTIFICATION	0xd1ce004e
#define SNDRV_FIREWIRE_EVENT_EFW_RESPONSE	0x4e617475
#define SNDRV_FIREWIRE_EVENT_DIGI00X_MESSAGE	0x746e736c
#define SNDRV_FIREWIRE_EVENT_MOTU_NOTIFICATION	0x64776479
#define SNDRV_FIREWIRE_EVENT_TASCAM_CONTROL	0x7473636d
#define SNDRV_FIREWIRE_EVENT_MOTU_REGISTER_DSP_CHANGE	0x4d545244

struct snd_firewire_event_common {
	unsigned int type; /* SNDRV_FIREWIRE_EVENT_xxx */
};

struct snd_firewire_event_lock_status {
	unsigned int type;
	unsigned int status; /* 0/1 = unlocked/locked */
};

struct snd_firewire_event_dice_notification {
	unsigned int type;
	unsigned int notification; /* DICE-specific bits */
};

#define SND_EFW_TRANSACTION_USER_SEQNUM_MAX	((__u32)((__u16)~0) - 1)
/* each field should be in big endian */
struct snd_efw_transaction {
	__be32 length;
	__be32 version;
	__be32 seqnum;
	__be32 category;
	__be32 command;
	__be32 status;
	__be32 params[0];
};
struct snd_firewire_event_efw_response {
	unsigned int type;
	__be32 response[0];	/* some responses */
};

struct snd_firewire_event_digi00x_message {
	unsigned int type;
	__u32 message;	/* Digi00x-specific message */
};

struct snd_firewire_event_motu_notification {
	unsigned int type;
	__u32 message;	/* MOTU-specific bits. */
};

struct snd_firewire_tascam_change {
	unsigned int index;
	__be32 before;
	__be32 after;
};

struct snd_firewire_event_tascam_control {
	unsigned int type;
	struct snd_firewire_tascam_change changes[0];
};

struct snd_firewire_event_motu_register_dsp_change {
	unsigned int type;
	__u32 count;		/* The number of changes. */
	__u32 changes[];	/* Encoded event for change of register DSP. */
};

union snd_firewire_event {
	struct snd_firewire_event_common            common;
	struct snd_firewire_event_lock_status       lock_status;
	struct snd_firewire_event_dice_notification dice_notification;
	struct snd_firewire_event_efw_response      efw_response;
	struct snd_firewire_event_digi00x_message   digi00x_message;
	struct snd_firewire_event_tascam_control    tascam_control;
	struct snd_firewire_event_motu_notification motu_notification;
	struct snd_firewire_event_motu_register_dsp_change motu_register_dsp_change;
};


#define SNDRV_FIREWIRE_IOCTL_GET_INFO _IOR('H', 0xf8, struct snd_firewire_get_info)
#define SNDRV_FIREWIRE_IOCTL_LOCK      _IO('H', 0xf9)
#define SNDRV_FIREWIRE_IOCTL_UNLOCK    _IO('H', 0xfa)
#define SNDRV_FIREWIRE_IOCTL_TASCAM_STATE _IOR('H', 0xfb, struct snd_firewire_tascam_state)
#define SNDRV_FIREWIRE_IOCTL_MOTU_REGISTER_DSP_METER	_IOR('H', 0xfc, struct snd_firewire_motu_register_dsp_meter)
#define SNDRV_FIREWIRE_IOCTL_MOTU_COMMAND_DSP_METER	_IOR('H', 0xfd, struct snd_firewire_motu_command_dsp_meter)
#define SNDRV_FIREWIRE_IOCTL_MOTU_REGISTER_DSP_PARAMETER	_IOR('H', 0xfe, struct snd_firewire_motu_register_dsp_parameter)

#define SNDRV_FIREWIRE_TYPE_DICE	1
#define SNDRV_FIREWIRE_TYPE_FIREWORKS	2
#define SNDRV_FIREWIRE_TYPE_BEBOB	3
#define SNDRV_FIREWIRE_TYPE_OXFW	4
#define SNDRV_FIREWIRE_TYPE_DIGI00X	5
#define SNDRV_FIREWIRE_TYPE_TASCAM	6
#define SNDRV_FIREWIRE_TYPE_MOTU	7
#define SNDRV_FIREWIRE_TYPE_FIREFACE	8

struct snd_firewire_get_info {
	unsigned int type; /* SNDRV_FIREWIRE_TYPE_xxx */
	unsigned int card; /* same as fw_cdev_get_info.card */
	unsigned char guid[8];
	char device_name[16]; /* device node in /dev */
};

/*
 * SNDRV_FIREWIRE_IOCTL_LOCK prevents the driver from streaming.
 * Returns -EBUSY if the driver is already streaming.
 */

#define SNDRV_FIREWIRE_TASCAM_STATE_COUNT	64

struct snd_firewire_tascam_state {
	__be32 data[SNDRV_FIREWIRE_TASCAM_STATE_COUNT];
};

/*
 * In below MOTU models, software is allowed to control their DSP by accessing to registers.
 *  - 828mk2
 *  - 896hd
 *  - Traveler
 *  - 8 pre
 *  - Ultralite
 *  - 4 pre
 *  - Audio Express
 *
 * On the other hand, the status of DSP is split into specific messages included in the sequence of
 * isochronous packet. ALSA firewire-motu driver gathers the messages and allow userspace applications
 * to read it via ioctl. In 828mk2, 896hd, and Traveler, hardware meter for all of physical inputs
 * are put into the message, while one pair of physical outputs is selected. The selection is done by
 * LSB one byte in asynchronous write quadlet transaction to 0x'ffff'f000'0b2c.
 *
 * I note that V3HD/V4HD uses asynchronous transaction for the purpose. The destination address is
 * registered to 0x'ffff'f000'0b38 and '0b3c by asynchronous write quadlet request. The size of
 * message differs between 23 and 51 quadlets. For the case, the number of mixer bus can be extended
 * up to 12.
 */

#define SNDRV_FIREWIRE_MOTU_REGISTER_DSP_METER_COUNT	40

/**
 * struct snd_firewire_motu_register_dsp_meter - the container for meter information in DSP
 *						 controlled by register access
 * @data: Signal level meters. The mapping between position and input/output channel is
 *	  model-dependent.
 *
 * The structure expresses the part of DSP status for hardware meter. The u8 storage includes linear
 * value for audio signal level between 0x00 and 0x7f.
 */
struct snd_firewire_motu_register_dsp_meter {
	__u8 data[SNDRV_FIREWIRE_MOTU_REGISTER_DSP_METER_COUNT];
};

#define SNDRV_FIREWIRE_MOTU_REGISTER_DSP_MIXER_COUNT		4
#define SNDRV_FIREWIRE_MOTU_REGISTER_DSP_MIXER_SRC_COUNT	20
#define SNDRV_FIREWIRE_MOTU_REGISTER_DSP_INPUT_COUNT		10
#define SNDRV_FIREWIRE_MOTU_REGISTER_DSP_ALIGNED_INPUT_COUNT	(SNDRV_FIREWIRE_MOTU_REGISTER_DSP_INPUT_COUNT + 2)

/**
 * snd_firewire_motu_register_dsp_parameter - the container for parameters of DSP controlled
 *					      by register access.
 * @mixer.source.gain: The gain of source to mixer.
 * @mixer.source.pan: The L/R balance of source to mixer.
 * @mixer.source.flag: The flag of source to mixer, including mute, solo.
 * @mixer.source.paired_balance: The L/R balance of paired source to mixer, only for 4 pre and
 *				 Audio Express.
 * @mixer.source.paired_width: The width of paired source to mixer, only for 4 pre and
 *			       Audio Express.
 * @mixer.output.paired_volume: The volume of paired output from mixer.
 * @mixer.output.paired_flag: The flag of paired output from mixer.
 * @output.main_paired_volume: The volume of paired main output.
 * @output.hp_paired_volume: The volume of paired hp output.
 * @output.hp_paired_assignment: The source assigned to paired hp output.
 * @output.reserved: Padding for 32 bit alignment for future extension.
 * @line_input.boost_flag: The flags of boost for line inputs, only for 828mk2 and Traveler.
 * @line_input.nominal_level_flag: The flags of nominal level for line inputs, only for 828mk2 and
 *				   Traveler.
 * @line_input.reserved: Padding for 32 bit alignment for future extension.
 * @input.gain_and_invert: The value including gain and invert for input, only for Ultralite, 4 pre
 *			   and Audio Express.
 * @input.flag: The flag of input; e.g. jack detection, phantom power, and pad, only for Ultralite,
 *		4 pre and Audio express.
 * @reserved: Padding so that the size of structure is kept to 512 byte, but for future extension.
 *
 * The structure expresses the set of parameters for DSP controlled by register access.
 */
struct snd_firewire_motu_register_dsp_parameter {
	struct {
		struct {
			__u8 gain[SNDRV_FIREWIRE_MOTU_REGISTER_DSP_MIXER_SRC_COUNT];
			__u8 pan[SNDRV_FIREWIRE_MOTU_REGISTER_DSP_MIXER_SRC_COUNT];
			__u8 flag[SNDRV_FIREWIRE_MOTU_REGISTER_DSP_MIXER_SRC_COUNT];
			__u8 paired_balance[SNDRV_FIREWIRE_MOTU_REGISTER_DSP_MIXER_SRC_COUNT];
			__u8 paired_width[SNDRV_FIREWIRE_MOTU_REGISTER_DSP_MIXER_SRC_COUNT];
		} source[SNDRV_FIREWIRE_MOTU_REGISTER_DSP_MIXER_COUNT];
		struct {
			__u8 paired_volume[SNDRV_FIREWIRE_MOTU_REGISTER_DSP_MIXER_COUNT];
			__u8 paired_flag[SNDRV_FIREWIRE_MOTU_REGISTER_DSP_MIXER_COUNT];
		} output;
	} mixer;
	struct {
		__u8 main_paired_volume;
		__u8 hp_paired_volume;
		__u8 hp_paired_assignment;
		__u8 reserved[5];
	} output;
	struct {
		__u8 boost_flag;
		__u8 nominal_level_flag;
		__u8 reserved[6];
	} line_input;
	struct {
		__u8 gain_and_invert[SNDRV_FIREWIRE_MOTU_REGISTER_DSP_ALIGNED_INPUT_COUNT];
		__u8 flag[SNDRV_FIREWIRE_MOTU_REGISTER_DSP_ALIGNED_INPUT_COUNT];
	} input;
	__u8 reserved[64];
};

/*
 * In below MOTU models, software is allowed to control their DSP by command in frame of
 * asynchronous transaction to 0x'ffff'0001'0000:
 *
 *  - 828 mk3 (FireWire only and Hybrid)
 *  - 896 mk3 (FireWire only and Hybrid)
 *  - Ultralite mk3 (FireWire only and Hybrid)
 *  - Traveler mk3
 *  - Track 16
 *
 * On the other hand, the states of hardware meter is split into specific messages included in the
 * sequence of isochronous packet. ALSA firewire-motu driver gathers the message and allow userspace
 * application to read it via ioctl.
 */

#define SNDRV_FIREWIRE_MOTU_COMMAND_DSP_METER_COUNT	400

/**
 * struct snd_firewire_motu_command_dsp_meter - the container for meter information in DSP
 *						controlled by command
 * @data: Signal level meters. The mapping between position and signal channel is model-dependent.
 *
 * The structure expresses the part of DSP status for hardware meter. The 32 bit storage is
 * estimated to include IEEE 764 32 bit single precision floating point (binary32) value. It is
 * expected to be linear value (not logarithm) for audio signal level between 0.0 and +1.0. However,
 * the last two quadlets (data[398] and data[399]) are filled with 0xffffffff since they are the
 * marker of one period.
 */
struct snd_firewire_motu_command_dsp_meter {
	__u32 data[SNDRV_FIREWIRE_MOTU_COMMAND_DSP_METER_COUNT];
};

#endif /* _UAPI_SOUND_FIREWIRE_H_INCLUDED */
