// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Universal MIDI Packet (UMP): Message Definitions
 */
#ifndef __SOUND_UMP_MSG_H
#define __SOUND_UMP_MSG_H

/* MIDI 1.0 / 2.0 Status Code (4bit) */
enum {
	UMP_MSG_STATUS_PER_NOTE_RCC = 0x0,
	UMP_MSG_STATUS_PER_NOTE_ACC = 0x1,
	UMP_MSG_STATUS_RPN = 0x2,
	UMP_MSG_STATUS_NRPN = 0x3,
	UMP_MSG_STATUS_RELATIVE_RPN = 0x4,
	UMP_MSG_STATUS_RELATIVE_NRPN = 0x5,
	UMP_MSG_STATUS_PER_NOTE_PITCH_BEND = 0x6,
	UMP_MSG_STATUS_NOTE_OFF = 0x8,
	UMP_MSG_STATUS_NOTE_ON = 0x9,
	UMP_MSG_STATUS_POLY_PRESSURE = 0xa,
	UMP_MSG_STATUS_CC = 0xb,
	UMP_MSG_STATUS_PROGRAM = 0xc,
	UMP_MSG_STATUS_CHANNEL_PRESSURE = 0xd,
	UMP_MSG_STATUS_PITCH_BEND = 0xe,
	UMP_MSG_STATUS_PER_NOTE_MGMT = 0xf,
};

/* MIDI 1.0 Channel Control (7bit) */
enum {
	UMP_CC_BANK_SELECT = 0,
	UMP_CC_MODULATION = 1,
	UMP_CC_BREATH = 2,
	UMP_CC_FOOT = 4,
	UMP_CC_PORTAMENTO_TIME = 5,
	UMP_CC_DATA = 6,
	UMP_CC_VOLUME = 7,
	UMP_CC_BALANCE = 8,
	UMP_CC_PAN = 10,
	UMP_CC_EXPRESSION = 11,
	UMP_CC_EFFECT_CONTROL_1 = 12,
	UMP_CC_EFFECT_CONTROL_2 = 13,
	UMP_CC_GP_1 = 16,
	UMP_CC_GP_2 = 17,
	UMP_CC_GP_3 = 18,
	UMP_CC_GP_4 = 19,
	UMP_CC_BANK_SELECT_LSB = 32,
	UMP_CC_MODULATION_LSB = 33,
	UMP_CC_BREATH_LSB = 34,
	UMP_CC_FOOT_LSB = 36,
	UMP_CC_PORTAMENTO_TIME_LSB = 37,
	UMP_CC_DATA_LSB = 38,
	UMP_CC_VOLUME_LSB = 39,
	UMP_CC_BALANCE_LSB = 40,
	UMP_CC_PAN_LSB = 42,
	UMP_CC_EXPRESSION_LSB = 43,
	UMP_CC_EFFECT1_LSB = 44,
	UMP_CC_EFFECT2_LSB = 45,
	UMP_CC_GP_1_LSB = 48,
	UMP_CC_GP_2_LSB = 49,
	UMP_CC_GP_3_LSB = 50,
	UMP_CC_GP_4_LSB = 51,
	UMP_CC_SUSTAIN = 64,
	UMP_CC_PORTAMENTO_SWITCH = 65,
	UMP_CC_SOSTENUTO = 66,
	UMP_CC_SOFT_PEDAL = 67,
	UMP_CC_LEGATO = 68,
	UMP_CC_HOLD_2 = 69,
	UMP_CC_SOUND_CONTROLLER_1 = 70,
	UMP_CC_SOUND_CONTROLLER_2 = 71,
	UMP_CC_SOUND_CONTROLLER_3 = 72,
	UMP_CC_SOUND_CONTROLLER_4 = 73,
	UMP_CC_SOUND_CONTROLLER_5 = 74,
	UMP_CC_SOUND_CONTROLLER_6 = 75,
	UMP_CC_SOUND_CONTROLLER_7 = 76,
	UMP_CC_SOUND_CONTROLLER_8 = 77,
	UMP_CC_SOUND_CONTROLLER_9 = 78,
	UMP_CC_SOUND_CONTROLLER_10 = 79,
	UMP_CC_GP_5 = 80,
	UMP_CC_GP_6 = 81,
	UMP_CC_GP_7 = 82,
	UMP_CC_GP_8 = 83,
	UMP_CC_PORTAMENTO_CONTROL = 84,
	UMP_CC_EFFECT_1 = 91,
	UMP_CC_EFFECT_2 = 92,
	UMP_CC_EFFECT_3 = 93,
	UMP_CC_EFFECT_4 = 94,
	UMP_CC_EFFECT_5 = 95,
	UMP_CC_DATA_INC = 96,
	UMP_CC_DATA_DEC = 97,
	UMP_CC_NRPN_LSB = 98,
	UMP_CC_NRPN_MSB = 99,
	UMP_CC_RPN_LSB = 100,
	UMP_CC_RPN_MSB = 101,
	UMP_CC_ALL_SOUND_OFF = 120,
	UMP_CC_RESET_ALL = 121,
	UMP_CC_LOCAL_CONTROL = 122,
	UMP_CC_ALL_NOTES_OFF = 123,
	UMP_CC_OMNI_OFF = 124,
	UMP_CC_OMNI_ON = 125,
	UMP_CC_POLY_OFF = 126,
	UMP_CC_POLY_ON = 127,
};

/* MIDI 1.0 / 2.0 System Messages (0xfx) */
enum {
	UMP_SYSTEM_STATUS_MIDI_TIME_CODE = 0xf1,
	UMP_SYSTEM_STATUS_SONG_POSITION = 0xf2,
	UMP_SYSTEM_STATUS_SONG_SELECT = 0xf3,
	UMP_SYSTEM_STATUS_TUNE_REQUEST = 0xf6,
	UMP_SYSTEM_STATUS_TIMING_CLOCK = 0xf8,
	UMP_SYSTEM_STATUS_START = 0xfa,
	UMP_SYSTEM_STATUS_CONTINUE = 0xfb,
	UMP_SYSTEM_STATUS_STOP = 0xfc,
	UMP_SYSTEM_STATUS_ACTIVE_SENSING = 0xfe,
	UMP_SYSTEM_STATUS_RESET = 0xff,
};

/* MIDI 1.0 Realtime and SysEx status messages (0xfx) */
enum {
	UMP_MIDI1_MSG_REALTIME		= 0xf0,	/* mask */
	UMP_MIDI1_MSG_SYSEX_START	= 0xf0,
	UMP_MIDI1_MSG_SYSEX_END		= 0xf7,
};

/*
 * UMP Message Definitions
 */

/* MIDI 1.0 Note Off / Note On (32bit) */
struct snd_ump_midi1_msg_note {
#ifdef __BIG_ENDIAN_BITFIELD
	u32 type:4;
	u32 group:4;
	u32 status:4;
	u32 channel:4;
	u32 note:8;
	u32 velocity:8;
#else
	u32 velocity:8;
	u32 note:8;
	u32 channel:4;
	u32 status:4;
	u32 group:4;
	u32 type:4;
#endif
} __packed;

/* MIDI 1.0 Poly Pressure (32bit) */
struct snd_ump_midi1_msg_paf {
#ifdef __BIG_ENDIAN_BITFIELD
	u32 type:4;
	u32 group:4;
	u32 status:4;
	u32 channel:4;
	u32 note:8;
	u32 data:8;
#else
	u32 data:8;
	u32 note:8;
	u32 channel:4;
	u32 status:4;
	u32 group:4;
	u32 type:4;
#endif
} __packed;

/* MIDI 1.0 Control Change (32bit) */
struct snd_ump_midi1_msg_cc {
#ifdef __BIG_ENDIAN_BITFIELD
	u32 type:4;
	u32 group:4;
	u32 status:4;
	u32 channel:4;
	u32 index:8;
	u32 data:8;
#else
	u32 data:8;
	u32 index:8;
	u32 channel:4;
	u32 status:4;
	u32 group:4;
	u32 type:4;
#endif
} __packed;

/* MIDI 1.0 Program Change (32bit) */
struct snd_ump_midi1_msg_program {
#ifdef __BIG_ENDIAN_BITFIELD
	u32 type:4;
	u32 group:4;
	u32 status:4;
	u32 channel:4;
	u32 program:8;
	u32 reserved:8;
#else
	u32 reserved:8;
	u32 program:8;
	u32 channel:4;
	u32 status:4;
	u32 group:4;
	u32 type:4;
#endif
} __packed;

/* MIDI 1.0 Channel Pressure (32bit) */
struct snd_ump_midi1_msg_caf {
#ifdef __BIG_ENDIAN_BITFIELD
	u32 type:4;
	u32 group:4;
	u32 status:4;
	u32 channel:4;
	u32 data:8;
	u32 reserved:8;
#else
	u32 reserved:8;
	u32 data:8;
	u32 channel:4;
	u32 status:4;
	u32 group:4;
	u32 type:4;
#endif
} __packed;

/* MIDI 1.0 Pitch Bend (32bit) */
struct snd_ump_midi1_msg_pitchbend {
#ifdef __BIG_ENDIAN_BITFIELD
	u32 type:4;
	u32 group:4;
	u32 status:4;
	u32 channel:4;
	u32 data_lsb:8;
	u32 data_msb:8;
#else
	u32 data_msb:8;
	u32 data_lsb:8;
	u32 channel:4;
	u32 status:4;
	u32 group:4;
	u32 type:4;
#endif
} __packed;

/* System Common and Real Time messages (32bit); no channel field */
struct snd_ump_system_msg {
#ifdef __BIG_ENDIAN_BITFIELD
	u32 type:4;
	u32 group:4;
	u32 status:8;
	u32 parm1:8;
	u32 parm2:8;
#else
	u32 parm2:8;
	u32 parm1:8;
	u32 status:8;
	u32 group:4;
	u32 type:4;
#endif
} __packed;

/* MIDI 1.0 UMP CVM (32bit) */
union snd_ump_midi1_msg {
	struct snd_ump_midi1_msg_note note;
	struct snd_ump_midi1_msg_paf paf;
	struct snd_ump_midi1_msg_cc cc;
	struct snd_ump_midi1_msg_program pg;
	struct snd_ump_midi1_msg_caf caf;
	struct snd_ump_midi1_msg_pitchbend pb;
	struct snd_ump_system_msg system;
	u32 raw;
};

/* MIDI 2.0 Note Off / Note On (64bit) */
struct snd_ump_midi2_msg_note {
#ifdef __BIG_ENDIAN_BITFIELD
	/* 0 */
	u32 type:4;
	u32 group:4;
	u32 status:4;
	u32 channel:4;
	u32 note:8;
	u32 attribute_type:8;
	/* 1 */
	u32 velocity:16;
	u32 attribute_data:16;
#else
	/* 0 */
	u32 attribute_type:8;
	u32 note:8;
	u32 channel:4;
	u32 status:4;
	u32 group:4;
	u32 type:4;
	/* 1 */
	u32 attribute_data:16;
	u32 velocity:16;
#endif
} __packed;

/* MIDI 2.0 Poly Pressure (64bit) */
struct snd_ump_midi2_msg_paf {
#ifdef __BIG_ENDIAN_BITFIELD
	/* 0 */
	u32 type:4;
	u32 group:4;
	u32 status:4;
	u32 channel:4;
	u32 note:8;
	u32 reserved:8;
	/* 1 */
	u32 data;
#else
	/* 0 */
	u32 reserved:8;
	u32 note:8;
	u32 channel:4;
	u32 status:4;
	u32 group:4;
	u32 type:4;
	/* 1 */
	u32 data;
#endif
} __packed;

/* MIDI 2.0 Per-Note Controller (64bit) */
struct snd_ump_midi2_msg_pernote_cc {
#ifdef __BIG_ENDIAN_BITFIELD
	/* 0 */
	u32 type:4;
	u32 group:4;
	u32 status:4;
	u32 channel:4;
	u32 note:8;
	u32 index:8;
	/* 1 */
	u32 data;
#else
	/* 0 */
	u32 index:8;
	u32 note:8;
	u32 channel:4;
	u32 status:4;
	u32 group:4;
	u32 type:4;
	/* 1 */
	u32 data;
#endif
} __packed;

/* MIDI 2.0 Per-Note Management (64bit) */
struct snd_ump_midi2_msg_pernote_mgmt {
#ifdef __BIG_ENDIAN_BITFIELD
	/* 0 */
	u32 type:4;
	u32 group:4;
	u32 status:4;
	u32 channel:4;
	u32 note:8;
	u32 flags:8;
	/* 1 */
	u32 reserved;
#else
	/* 0 */
	u32 flags:8;
	u32 note:8;
	u32 channel:4;
	u32 status:4;
	u32 group:4;
	u32 type:4;
	/* 1 */
	u32 reserved;
#endif
} __packed;

/* MIDI 2.0 Control Change (64bit) */
struct snd_ump_midi2_msg_cc {
#ifdef __BIG_ENDIAN_BITFIELD
	/* 0 */
	u32 type:4;
	u32 group:4;
	u32 status:4;
	u32 channel:4;
	u32 index:8;
	u32 reserved:8;
	/* 1 */
	u32 data;
#else
	/* 0 */
	u32 reserved:8;
	u32 index:8;
	u32 channel:4;
	u32 status:4;
	u32 group:4;
	u32 type:4;
	/* 1 */
	u32 data;
#endif
} __packed;

/* MIDI 2.0 Registered Controller (RPN) / Assignable Controller (NRPN) (64bit) */
struct snd_ump_midi2_msg_rpn {
#ifdef __BIG_ENDIAN_BITFIELD
	/* 0 */
	u32 type:4;
	u32 group:4;
	u32 status:4;
	u32 channel:4;
	u32 bank:8;
	u32 index:8;
	/* 1 */
	u32 data;
#else
	/* 0 */
	u32 index:8;
	u32 bank:8;
	u32 channel:4;
	u32 status:4;
	u32 group:4;
	u32 type:4;
	/* 1 */
	u32 data;
#endif
} __packed;

/* MIDI 2.0 Program Change (64bit) */
struct snd_ump_midi2_msg_program {
#ifdef __BIG_ENDIAN_BITFIELD
	/* 0 */
	u32 type:4;
	u32 group:4;
	u32 status:4;
	u32 channel:4;
	u32 reserved:15;
	u32 bank_valid:1;
	/* 1 */
	u32 program:8;
	u32 reserved2:8;
	u32 bank_msb:8;
	u32 bank_lsb:8;
#else
	/* 0 */
	u32 bank_valid:1;
	u32 reserved:15;
	u32 channel:4;
	u32 status:4;
	u32 group:4;
	u32 type:4;
	/* 1 */
	u32 bank_lsb:8;
	u32 bank_msb:8;
	u32 reserved2:8;
	u32 program:8;
#endif
} __packed;

/* MIDI 2.0 Channel Pressure (64bit) */
struct snd_ump_midi2_msg_caf {
#ifdef __BIG_ENDIAN_BITFIELD
	/* 0 */
	u32 type:4;
	u32 group:4;
	u32 status:4;
	u32 channel:4;
	u32 reserved:16;
	/* 1 */
	u32 data;
#else
	/* 0 */
	u32 reserved:16;
	u32 channel:4;
	u32 status:4;
	u32 group:4;
	u32 type:4;
	/* 1 */
	u32 data;
#endif
} __packed;

/* MIDI 2.0 Pitch Bend (64bit) */
struct snd_ump_midi2_msg_pitchbend {
#ifdef __BIG_ENDIAN_BITFIELD
	/* 0 */
	u32 type:4;
	u32 group:4;
	u32 status:4;
	u32 channel:4;
	u32 reserved:16;
	/* 1 */
	u32 data;
#else
	/* 0 */
	u32 reserved:16;
	u32 channel:4;
	u32 status:4;
	u32 group:4;
	u32 type:4;
	/* 1 */
	u32 data;
#endif
} __packed;

/* MIDI 2.0 Per-Note Pitch Bend (64bit) */
struct snd_ump_midi2_msg_pernote_pitchbend {
#ifdef __BIG_ENDIAN_BITFIELD
	/* 0 */
	u32 type:4;
	u32 group:4;
	u32 status:4;
	u32 channel:4;
	u32 note:8;
	u32 reserved:8;
	/* 1 */
	u32 data;
#else
	/* 0 */
	u32 reserved:8;
	u32 note:8;
	u32 channel:4;
	u32 status:4;
	u32 group:4;
	u32 type:4;
	/* 1 */
	u32 data;
#endif
} __packed;

/* MIDI 2.0 UMP CVM (64bit) */
union snd_ump_midi2_msg {
	struct snd_ump_midi2_msg_note note;
	struct snd_ump_midi2_msg_paf paf;
	struct snd_ump_midi2_msg_pernote_cc pernote_cc;
	struct snd_ump_midi2_msg_pernote_mgmt pernote_mgmt;
	struct snd_ump_midi2_msg_cc cc;
	struct snd_ump_midi2_msg_rpn rpn;
	struct snd_ump_midi2_msg_program pg;
	struct snd_ump_midi2_msg_caf caf;
	struct snd_ump_midi2_msg_pitchbend pb;
	struct snd_ump_midi2_msg_pernote_pitchbend pernote_pb;
	u32 raw[2];
};

/* UMP Stream Message: Endpoint Discovery (128bit) */
struct snd_ump_stream_msg_ep_discovery {
#ifdef __BIG_ENDIAN_BITFIELD
	/* 0 */
	u32 type:4;
	u32 format:2;
	u32 status:10;
	u32 ump_version_major:8;
	u32 ump_version_minor:8;
	/* 1 */
	u32 reserved:24;
	u32 filter_bitmap:8;
	/* 2-3 */
	u32 reserved2[2];
#else
	/* 0 */
	u32 ump_version_minor:8;
	u32 ump_version_major:8;
	u32 status:10;
	u32 format:2;
	u32 type:4;
	/* 1 */
	u32 filter_bitmap:8;
	u32 reserved:24;
	/* 2-3 */
	u32 reserved2[2];
#endif
} __packed;

/* UMP Stream Message: Endpoint Info Notification (128bit) */
struct snd_ump_stream_msg_ep_info {
#ifdef __BIG_ENDIAN_BITFIELD
	/* 0 */
	u32 type:4;
	u32 format:2;
	u32 status:10;
	u32 ump_version_major:8;
	u32 ump_version_minor:8;
	/* 1 */
	u32 static_function_block:1;
	u32 num_function_blocks:7;
	u32 reserved:8;
	u32 protocol:8;
	u32 reserved2:6;
	u32 jrts:2;
	/* 2-3 */
	u32 reserved3[2];
#else
	/* 0 */
	u32 ump_version_minor:8;
	u32 ump_version_major:8;
	u32 status:10;
	u32 format:2;
	u32 type:4;
	/* 1 */
	u32 jrts:2;
	u32 reserved2:6;
	u32 protocol:8;
	u32 reserved:8;
	u32 num_function_blocks:7;
	u32 static_function_block:1;
	/* 2-3 */
	u32 reserved3[2];
#endif
} __packed;

/* UMP Stream Message: Device Info Notification (128bit) */
struct snd_ump_stream_msg_devince_info {
#ifdef __BIG_ENDIAN_BITFIELD
	/* 0 */
	u32 type:4;
	u32 format:2;
	u32 status:10;
	u32 reserved:16;
	/* 1 */
	u32 manufacture_id;
	/* 2 */
	u8 family_lsb;
	u8 family_msb;
	u8 model_lsb;
	u8 model_msb;
	/* 3 */
	u32 sw_revision;
#else
	/* 0 */
	u32 reserved:16;
	u32 status:10;
	u32 format:2;
	u32 type:4;
	/* 1 */
	u32 manufacture_id;
	/* 2 */
	u8 model_msb;
	u8 model_lsb;
	u8 family_msb;
	u8 family_lsb;
	/* 3 */
	u32 sw_revision;
#endif
} __packed;

/* UMP Stream Message: Stream Config Request / Notification (128bit) */
struct snd_ump_stream_msg_stream_cfg {
#ifdef __BIG_ENDIAN_BITFIELD
	/* 0 */
	u32 type:4;
	u32 format:2;
	u32 status:10;
	u32 protocol:8;
	u32 reserved:6;
	u32 jrts:2;
	/* 1-3 */
	u32 reserved2[3];
#else
	/* 0 */
	u32 jrts:2;
	u32 reserved:6;
	u32 protocol:8;
	u32 status:10;
	u32 format:2;
	u32 type:4;
	/* 1-3 */
	u32 reserved2[3];
#endif
} __packed;

/* UMP Stream Message: Function Block Discovery (128bit) */
struct snd_ump_stream_msg_fb_discovery {
#ifdef __BIG_ENDIAN_BITFIELD
	/* 0 */
	u32 type:4;
	u32 format:2;
	u32 status:10;
	u32 function_block_id:8;
	u32 filter:8;
	/* 1-3 */
	u32 reserved[3];
#else
	/* 0 */
	u32 filter:8;
	u32 function_block_id:8;
	u32 status:10;
	u32 format:2;
	u32 type:4;
	/* 1-3 */
	u32 reserved[3];
#endif
} __packed;

/* UMP Stream Message: Function Block Info Notification (128bit) */
struct snd_ump_stream_msg_fb_info {
#ifdef __BIG_ENDIAN_BITFIELD
	/* 0 */
	u32 type:4;
	u32 format:2;
	u32 status:10;
	u32 active:1;
	u32 function_block_id:7;
	u32 reserved:2;
	u32 ui_hint:2;
	u32 midi_10:2;
	u32 direction:2;
	/* 1 */
	u32 first_group:8;
	u32 num_groups:8;
	u32 midi_ci_version:8;
	u32 sysex8_streams:8;
	/* 2-3 */
	u32 reserved2[2];
#else
	/* 0 */
	u32 direction:2;
	u32 midi_10:2;
	u32 ui_hint:2;
	u32 reserved:2;
	u32 function_block_id:7;
	u32 active:1;
	u32 status:10;
	u32 format:2;
	u32 type:4;
	/* 1 */
	u32 sysex8_streams:8;
	u32 midi_ci_version:8;
	u32 num_groups:8;
	u32 first_group:8;
	/* 2-3 */
	u32 reserved2[2];
#endif
} __packed;

/* UMP Stream Message: Function Block Name Notification (128bit) */
struct snd_ump_stream_msg_fb_name {
#ifdef __BIG_ENDIAN_BITFIELD
	/* 0 */
	u16 type:4;
	u16 format:2;
	u16 status:10;
	u8 function_block_id;
	u8 name0;
	/* 1-3 */
	u8 name[12];
#else
	/* 0 */
	u8 name0;
	u8 function_block_id;
	u16 status:10;
	u16 format:2;
	u16 type:4;
	/* 1-3 */
	u8 name[12]; // FIXME: byte order
#endif
} __packed;

/* MIDI 2.0 Stream Messages (128bit) */
union snd_ump_stream_msg {
	struct snd_ump_stream_msg_ep_discovery ep_discovery;
	struct snd_ump_stream_msg_ep_info ep_info;
	struct snd_ump_stream_msg_devince_info device_info;
	struct snd_ump_stream_msg_stream_cfg stream_cfg;
	struct snd_ump_stream_msg_fb_discovery fb_discovery;
	struct snd_ump_stream_msg_fb_info fb_info;
	struct snd_ump_stream_msg_fb_name fb_name;
	u32 raw[4];
};

#endif /* __SOUND_UMP_MSG_H */
