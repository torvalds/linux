/*
 * Driver for Digigram miXart soundcards
 *
 * low level interface with interrupt handling and mail box implementation
 *
 * Copyright (c) 2003 by Digigram <alsa@digigram.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef __SOUND_MIXART_CORE_H
#define __SOUND_MIXART_CORE_H


enum mixart_message_id {
	MSG_CONNECTOR_GET_AUDIO_INFO         = 0x050008,
	MSG_CONNECTOR_GET_OUT_AUDIO_LEVEL    = 0x050009,
	MSG_CONNECTOR_SET_OUT_AUDIO_LEVEL    = 0x05000A,

	MSG_CONSOLE_MANAGER                  = 0x070000,
	MSG_CONSOLE_GET_CLOCK_UID            = 0x070003,

	MSG_PHYSICALIO_SET_LEVEL             = 0x0F0008,

	MSG_STREAM_ADD_INPUT_GROUP           = 0x130000,
	MSG_STREAM_ADD_OUTPUT_GROUP          = 0x130001,
	MSG_STREAM_DELETE_GROUP              = 0x130004,
	MSG_STREAM_START_STREAM_GRP_PACKET   = 0x130006,
	MSG_STREAM_START_INPUT_STAGE_PACKET  = 0x130007,
	MSG_STREAM_START_OUTPUT_STAGE_PACKET = 0x130008,
	MSG_STREAM_STOP_STREAM_GRP_PACKET    = 0x130009,
	MSG_STREAM_STOP_INPUT_STAGE_PACKET   = 0x13000A,
	MSG_STREAM_STOP_OUTPUT_STAGE_PACKET  = 0x13000B,
	MSG_STREAM_SET_INPUT_STAGE_PARAM     = 0x13000F,
	MSG_STREAM_SET_OUTPUT_STAGE_PARAM    = 0x130010,
	MSG_STREAM_SET_IN_AUDIO_LEVEL        = 0x130015,
	MSG_STREAM_SET_OUT_STREAM_LEVEL      = 0x130017,

	MSG_SYSTEM_FIRST_ID                  = 0x160000,
	MSG_SYSTEM_ENUM_PHYSICAL_IO          = 0x16000E,
	MSG_SYSTEM_ENUM_PLAY_CONNECTOR       = 0x160017,
	MSG_SYSTEM_ENUM_RECORD_CONNECTOR     = 0x160018,
	MSG_SYSTEM_WAIT_SYNCHRO_CMD          = 0x16002C,
	MSG_SYSTEM_SEND_SYNCHRO_CMD          = 0x16002D,

	MSG_SERVICES_TIMER_NOTIFY            = 0x1D0404,
	MSG_SERVICES_REPORT_TRACES           = 0x1D0700,

	MSG_CLOCK_CHECK_PROPERTIES           = 0x200001,
	MSG_CLOCK_SET_PROPERTIES             = 0x200002,
};


struct mixart_msg
{
	u32          message_id;
	struct mixart_uid uid;
	void*        data;
	size_t       size;
};

/* structs used to communicate with miXart */

struct mixart_enum_connector_resp
{
	u32  error_code;
	u32  first_uid_offset;
	u32  uid_count;
	u32  current_uid_index;
	struct mixart_uid uid[MIXART_MAX_PHYS_CONNECTORS];
} __attribute__((packed));


/* used for following struct */
#define MIXART_FLOAT_P_22_0_TO_HEX      0x41b00000  /* 22.0f */
#define MIXART_FLOAT_M_20_0_TO_HEX      0xc1a00000  /* -20.0f */
#define MIXART_FLOAT____0_0_TO_HEX      0x00000000  /* 0.0f */

struct mixart_audio_info_req
{
	u32 line_max_level;    /* float */
	u32 micro_max_level;   /* float */
	u32 cd_max_level;      /* float */
} __attribute__((packed));

struct mixart_analog_hw_info
{
	u32 is_present;
	u32 hw_connection_type;
	u32 max_level;         /* float */
	u32 min_var_level;     /* float */
	u32 max_var_level;     /* float */
	u32 step_var_level;    /* float */
	u32 fix_gain;          /* float */
	u32 zero_var;          /* float */
} __attribute__((packed));

struct mixart_digital_hw_info
{
	u32   hw_connection_type;
	u32   presence;
	u32   clock;
	u32   reserved;
} __attribute__((packed));

struct mixart_analog_info
{
	u32                     type_mask;
	struct mixart_analog_hw_info micro_info;
	struct mixart_analog_hw_info line_info;
	struct mixart_analog_hw_info cd_info;
	u32                     analog_level_present;
} __attribute__((packed));

struct mixart_digital_info
{
	u32 type_mask;
	struct mixart_digital_hw_info aes_info;
	struct mixart_digital_hw_info adat_info;
} __attribute__((packed));

struct mixart_audio_info
{
	u32                   clock_type_mask;
	struct mixart_analog_info  analog_info;
	struct mixart_digital_info digital_info;
} __attribute__((packed));

struct mixart_audio_info_resp
{
	u32                 txx_status;
	struct mixart_audio_info info;
} __attribute__((packed));


/* used for nb_bytes_max_per_sample */
#define MIXART_FLOAT_P__4_0_TO_HEX      0x40800000  /* +4.0f */
#define MIXART_FLOAT_P__8_0_TO_HEX      0x41000000  /* +8.0f */

struct mixart_stream_info
{
	u32 size_max_byte_frame;
	u32 size_max_sample_frame;
	u32 nb_bytes_max_per_sample;  /* float */
} __attribute__((packed));

/*  MSG_STREAM_ADD_INPUT_GROUP */
/*  MSG_STREAM_ADD_OUTPUT_GROUP */

struct mixart_streaming_group_req
{
	u32 stream_count;
	u32 channel_count;
	u32 user_grp_number;
	u32 first_phys_audio;
	u32 latency;
	struct mixart_stream_info stream_info[32];
	struct mixart_uid connector;
	u32 flow_entry[32];
} __attribute__((packed));

struct mixart_stream_desc
{
	struct mixart_uid stream_uid;
	u32          stream_desc;
} __attribute__((packed));

struct mixart_streaming_group
{
	u32                  status;
	struct mixart_uid    group;
	u32                  pipe_desc;
	u32                  stream_count;
	struct mixart_stream_desc stream[32];
} __attribute__((packed));

/* MSG_STREAM_DELETE_GROUP */

/* request : mixart_uid_t group */

struct mixart_delete_group_resp
{
	u32  status;
	u32  unused[2];
} __attribute__((packed));


/* 	MSG_STREAM_START_INPUT_STAGE_PACKET  = 0x130000 + 7,
	MSG_STREAM_START_OUTPUT_STAGE_PACKET = 0x130000 + 8,
	MSG_STREAM_STOP_INPUT_STAGE_PACKET   = 0x130000 + 10,
	MSG_STREAM_STOP_OUTPUT_STAGE_PACKET  = 0x130000 + 11,
 */

struct mixart_fx_couple_uid
{
	struct mixart_uid uid_fx_code;
	struct mixart_uid uid_fx_data;
} __attribute__((packed));

struct mixart_txx_stream_desc
{
	struct mixart_uid       uid_pipe;
	u32                     stream_idx;
	u32                     fx_number;
	struct mixart_fx_couple_uid  uid_fx[4];
} __attribute__((packed));

struct mixart_flow_info
{
	struct mixart_txx_stream_desc  stream_desc;
	u32                       flow_entry;
	u32                       flow_phy_addr;
} __attribute__((packed));

struct mixart_stream_state_req
{
	u32                 delayed;
	u64                 scheduler;
	u32                 reserved4np[3];
	u32                 stream_count;  /* set to 1 for instance */
	struct mixart_flow_info  stream_info;   /* could be an array[stream_count] */
} __attribute__((packed));

/* 	MSG_STREAM_START_STREAM_GRP_PACKET   = 0x130000 + 6
	MSG_STREAM_STOP_STREAM_GRP_PACKET    = 0x130000 + 9
 */

struct mixart_group_state_req
{
	u32           delayed;
	u64           scheduler;
	u32           reserved4np[2];
	u32           pipe_count;    /* set to 1 for instance */
	struct mixart_uid  pipe_uid[1];   /* could be an array[pipe_count] */
} __attribute__((packed));

struct mixart_group_state_resp
{
	u32           txx_status;
	u64           scheduler;
} __attribute__((packed));



/* Structures used by the MSG_SERVICES_TIMER_NOTIFY command */

struct mixart_sample_pos
{
	u32   buffer_id;
	u32   validity;
	u32   sample_pos_high_part;
	u32   sample_pos_low_part;
} __attribute__((packed));

struct mixart_timer_notify
{
	u32                  stream_count;
	struct mixart_sample_pos  streams[MIXART_MAX_STREAM_PER_CARD * MIXART_MAX_CARDS];
} __attribute__((packed));


/*	MSG_CONSOLE_GET_CLOCK_UID            = 0x070003,
 */

/* request is a uid with desc = MSG_CONSOLE_MANAGER | cardindex */

struct mixart_return_uid
{
	u32 error_code;
	struct mixart_uid uid;
} __attribute__((packed));

/*	MSG_CLOCK_CHECK_PROPERTIES           = 0x200001,
	MSG_CLOCK_SET_PROPERTIES             = 0x200002,
*/

enum mixart_clock_generic_type {
	CGT_NO_CLOCK,
	CGT_INTERNAL_CLOCK,
	CGT_PROGRAMMABLE_CLOCK,
	CGT_INTERNAL_ENSLAVED_CLOCK,
	CGT_EXTERNAL_CLOCK,
	CGT_CURRENT_CLOCK
};

enum mixart_clock_mode {
	CM_UNDEFINED,
	CM_MASTER,
	CM_SLAVE,
	CM_STANDALONE,
	CM_NOT_CONCERNED
};


struct mixart_clock_properties
{
	u32 error_code;
	u32 validation_mask;
	u32 frequency;
	u32 reference_frequency;
	u32 clock_generic_type;
	u32 clock_mode;
	struct mixart_uid uid_clock_source;
	struct mixart_uid uid_event_source;
	u32 event_mode;
	u32 synchro_signal_presence;
	u32 format;
	u32 board_mask;
	u32 nb_callers; /* set to 1 (see below) */
	struct mixart_uid uid_caller[1];
} __attribute__((packed));

struct mixart_clock_properties_resp
{
	u32 status;
	u32 clock_mode;
} __attribute__((packed));


/*	MSG_STREAM_SET_INPUT_STAGE_PARAM     = 0x13000F */
/*	MSG_STREAM_SET_OUTPUT_STAGE_PARAM    = 0x130010 */

enum mixart_coding_type {
	CT_NOT_DEFINED,
	CT_LINEAR,
	CT_MPEG_L1,
	CT_MPEG_L2,
	CT_MPEG_L3,
	CT_MPEG_L3_LSF,
	CT_GSM
};
enum mixart_sample_type {
	ST_NOT_DEFINED,
	ST_FLOATING_POINT_32BE,
	ST_FLOATING_POINT_32LE,
	ST_FLOATING_POINT_64BE,
	ST_FLOATING_POINT_64LE,
	ST_FIXED_POINT_8,
	ST_FIXED_POINT_16BE,
	ST_FIXED_POINT_16LE,
	ST_FIXED_POINT_24BE,
	ST_FIXED_POINT_24LE,
	ST_FIXED_POINT_32BE,
	ST_FIXED_POINT_32LE,
	ST_INTEGER_8,
	ST_INTEGER_16BE,
	ST_INTEGER_16LE,
	ST_INTEGER_24BE,
	ST_INTEGER_24LE,
	ST_INTEGER_32BE,
	ST_INTEGER_32LE
};

struct mixart_stream_param_desc
{
	u32 coding_type;  /* use enum mixart_coding_type */
	u32 sample_type;  /* use enum mixart_sample_type */

	union {
		struct {
			u32 linear_endian_ness;
			u32 linear_bits;
			u32 is_signed;
			u32 is_float;
		} linear_format_info;

		struct {
			u32 mpeg_layer;
			u32 mpeg_mode;
			u32 mpeg_mode_extension;
			u32 mpeg_pre_emphasis;
			u32 mpeg_has_padding_bit;
			u32 mpeg_has_crc;
			u32 mpeg_has_extension;
			u32 mpeg_is_original;
			u32 mpeg_has_copyright;
		} mpeg_format_info;
	} format_info;

	u32 delayed;
	u64 scheduler;
	u32 sample_size;
	u32 has_header;
	u32 has_suffix;
	u32 has_bitrate;
	u32 samples_per_frame;
	u32 bytes_per_frame;
	u32 bytes_per_sample;
	u32 sampling_freq;
	u32 number_of_channel;
	u32 stream_number;
	u32 buffer_size;
	u32 differed_time;
	u32 reserved4np[3];
	u32 pipe_count;                           /* set to 1 (array size !) */
	u32 stream_count;                         /* set to 1 (array size !) */
	struct mixart_txx_stream_desc stream_desc[1];  /* only one stream per command, but this could be an array */

} __attribute__((packed));


/*	MSG_CONNECTOR_GET_OUT_AUDIO_LEVEL    = 0x050009,
 */


struct mixart_get_out_audio_level
{
	u32 txx_status;
	u32 digital_level;   /* float */
	u32 analog_level;    /* float */
	u32 monitor_level;   /* float */
	u32 mute;
	u32 monitor_mute1;
	u32 monitor_mute2;
} __attribute__((packed));


/*	MSG_CONNECTOR_SET_OUT_AUDIO_LEVEL    = 0x05000A,
 */

/* used for valid_mask below */
#define MIXART_AUDIO_LEVEL_ANALOG_MASK	0x01
#define MIXART_AUDIO_LEVEL_DIGITAL_MASK	0x02
#define MIXART_AUDIO_LEVEL_MONITOR_MASK	0x04
#define MIXART_AUDIO_LEVEL_MUTE_MASK	0x08
#define MIXART_AUDIO_LEVEL_MUTE_M1_MASK	0x10
#define MIXART_AUDIO_LEVEL_MUTE_M2_MASK	0x20

struct mixart_set_out_audio_level
{
	u32 delayed;
	u64 scheduler;
	u32 valid_mask1;
	u32 valid_mask2;
	u32 digital_level;   /* float */
	u32 analog_level;    /* float */
	u32 monitor_level;   /* float */
	u32 mute;
	u32 monitor_mute1;
	u32 monitor_mute2;
	u32 reserved4np;
} __attribute__((packed));


/*	MSG_SYSTEM_ENUM_PHYSICAL_IO          = 0x16000E,
 */

#define MIXART_MAX_PHYS_IO  (MIXART_MAX_CARDS * 2 * 2) /* 4 * (analog+digital) * (playback+capture) */

struct mixart_uid_enumeration
{
	u32 error_code;
	u32 first_uid_offset;
	u32 nb_uid;
	u32 current_uid_index;
	struct mixart_uid uid[MIXART_MAX_PHYS_IO];
} __attribute__((packed));


/*	MSG_PHYSICALIO_SET_LEVEL             = 0x0F0008,
	MSG_PHYSICALIO_GET_LEVEL             = 0x0F000C,
*/

struct mixart_io_channel_level
{
	u32 analog_level;   /* float */
	u32 unused[2];
} __attribute__((packed));

struct mixart_io_level
{
	s32 channel; /* 0=left, 1=right, -1=both, -2=both same */
	struct mixart_io_channel_level level[2];
} __attribute__((packed));


/*	MSG_STREAM_SET_IN_AUDIO_LEVEL        = 0x130015,
 */

struct mixart_in_audio_level_info
{
	struct mixart_uid connector;
	u32 valid_mask1;
	u32 valid_mask2;
	u32 digital_level;
	u32 analog_level;
} __attribute__((packed));

struct mixart_set_in_audio_level_req
{
	u32 delayed;
	u64 scheduler;
	u32 audio_count;  /* set to <= 2 */
	u32 reserved4np;
	struct mixart_in_audio_level_info level[2];
} __attribute__((packed));

/* response is a 32 bit status */


/*	MSG_STREAM_SET_OUT_STREAM_LEVEL      = 0x130017,
 */

/* defines used for valid_mask1 */
#define MIXART_OUT_STREAM_SET_LEVEL_LEFT_AUDIO1		0x01
#define MIXART_OUT_STREAM_SET_LEVEL_LEFT_AUDIO2		0x02
#define MIXART_OUT_STREAM_SET_LEVEL_RIGHT_AUDIO1	0x04
#define MIXART_OUT_STREAM_SET_LEVEL_RIGHT_AUDIO2	0x08
#define MIXART_OUT_STREAM_SET_LEVEL_STREAM_1		0x10
#define MIXART_OUT_STREAM_SET_LEVEL_STREAM_2		0x20
#define MIXART_OUT_STREAM_SET_LEVEL_MUTE_1		0x40
#define MIXART_OUT_STREAM_SET_LEVEL_MUTE_2		0x80

struct mixart_out_stream_level_info
{
	u32 valid_mask1;
	u32 valid_mask2;
	u32 left_to_out1_level;
	u32 left_to_out2_level;
	u32 right_to_out1_level;
	u32 right_to_out2_level;
	u32 digital_level1;
	u32 digital_level2;
	u32 mute1;
	u32 mute2;
} __attribute__((packed));

struct mixart_set_out_stream_level
{
	struct mixart_txx_stream_desc desc;
	struct mixart_out_stream_level_info out_level;
} __attribute__((packed));

struct mixart_set_out_stream_level_req
{
	u32 delayed;
	u64 scheduler;
	u32 reserved4np[2];
	u32 nb_of_stream;  /* set to 1 */
	struct mixart_set_out_stream_level stream_level; /* could be an array */
} __attribute__((packed));

/* response to this request is a u32 status value */


/* exported */
void snd_mixart_init_mailbox(struct mixart_mgr *mgr);
void snd_mixart_exit_mailbox(struct mixart_mgr *mgr);

int  snd_mixart_send_msg(struct mixart_mgr *mgr, struct mixart_msg *request, int max_resp_size, void *resp_data);
int  snd_mixart_send_msg_wait_notif(struct mixart_mgr *mgr, struct mixart_msg *request, u32 notif_event);
int  snd_mixart_send_msg_nonblock(struct mixart_mgr *mgr, struct mixart_msg *request);

irqreturn_t snd_mixart_interrupt(int irq, void *dev_id);
irqreturn_t snd_mixart_threaded_irq(int irq, void *dev_id);

void snd_mixart_reset_board(struct mixart_mgr *mgr);

#endif /* __SOUND_MIXART_CORE_H */
