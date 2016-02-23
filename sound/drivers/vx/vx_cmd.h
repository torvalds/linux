/*
 * Driver for Digigram VX soundcards
 *
 * Definitions of DSP commands
 *
 * Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
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

#ifndef __VX_CMD_H
#define __VX_CMD_H

enum {
	CMD_VERSION,
	CMD_SUPPORTED,
	CMD_TEST_IT,
	CMD_SEND_IRQA,
	CMD_IBL,
	CMD_ASYNC,
	CMD_RES_PIPE,
	CMD_FREE_PIPE,
	CMD_CONF_PIPE,
	CMD_ABORT_CONF_PIPE,
	CMD_PARAM_OUTPUT_PIPE,
	CMD_STOP_PIPE,
	CMD_PIPE_STATE,
	CMD_PIPE_SPL_COUNT,
	CMD_CAN_START_PIPE,
	CMD_SIZE_HBUFFER,
	CMD_START_STREAM,
	CMD_START_ONE_STREAM,
	CMD_PAUSE_STREAM,
	CMD_PAUSE_ONE_STREAM,
	CMD_STREAM_OUT_LEVEL_ADJUST,
	CMD_STOP_STREAM,
	CMD_FORMAT_STREAM_OUT,
	CMD_FORMAT_STREAM_IN,
	CMD_GET_STREAM_STATE,
	CMD_DROP_BYTES_AWAY,
	CMD_GET_REMAINING_BYTES,
	CMD_CONNECT_AUDIO,
	CMD_AUDIO_LEVEL_ADJUST,
	CMD_AUDIO_VU_PIC_METER,
	CMD_GET_AUDIO_LEVELS,
	CMD_GET_NOTIFY_EVENT,
	CMD_INFO_NOTIFIED,
	CMD_ACCESS_IO_FCT,
	CMD_STATUS_R_BUFFERS,
	CMD_UPDATE_R_BUFFERS,
	CMD_LOAD_EFFECT_CONTEXT,
	CMD_EFFECT_ONE_PIPE,
	CMD_MODIFY_CLOCK,
	CMD_STREAM1_OUT_SET_N_LEVELS,
	CMD_PURGE_STREAM_DCMDS,
	CMD_NOTIFY_PIPE_TIME,
	CMD_LOAD_EFFECT_CONTEXT_PACKET,
	CMD_RELIC_R_BUFFER,
	CMD_RESYNC_AUDIO_INPUTS,
	CMD_NOTIFY_STREAM_TIME,
	CMD_STREAM_SAMPLE_COUNT,
	CMD_CONFIG_TIME_CODE,
	CMD_GET_TIME_CODE,
	CMD_MANAGE_SIGNAL,
	CMD_PARAMETER_STREAM_OUT,
	CMD_READ_BOARD_FREQ,
	CMD_GET_STREAM_LEVELS,
	CMD_PURGE_PIPE_DCMDS,
	// CMD_SET_STREAM_OUT_EFFECTS,
	// CMD_GET_STREAM_OUT_EFFECTS,
	CMD_CONNECT_MONITORING,
	CMD_STREAM2_OUT_SET_N_LEVELS,
	CMD_CANCEL_R_BUFFERS,
	CMD_NOTIFY_END_OF_BUFFER,
	CMD_GET_STREAM_VU_METER,
	CMD_LAST_INDEX
};

struct vx_cmd_info {
	unsigned int opcode;	/* command word */
	int length;		/* command length (in words) */
	int st_type;		/* status type (RMH_SSIZE_XXX) */
	int st_length;		/* fixed length */
};

/* Family and code op of some DSP requests. */
#define CODE_OP_PIPE_TIME                       0x004e0000
#define CODE_OP_START_STREAM                    0x00800000
#define CODE_OP_PAUSE_STREAM                    0x00810000
#define CODE_OP_OUT_STREAM_LEVEL                0x00820000
#define CODE_OP_UPDATE_R_BUFFERS                0x00840000
#define CODE_OP_OUT_STREAM1_LEVEL_CURVE         0x00850000
#define CODE_OP_OUT_STREAM2_LEVEL_CURVE         0x00930000
#define CODE_OP_OUT_STREAM_FORMAT               0x00860000
#define CODE_OP_STREAM_TIME                     0x008f0000
#define CODE_OP_OUT_STREAM_EXTRAPARAMETER       0x00910000
#define CODE_OP_OUT_AUDIO_LEVEL                 0x00c20000

#define NOTIFY_LAST_COMMAND     0x00400000

/* Values for a user delay */
#define DC_DIFFERED_DELAY       (1<<BIT_DIFFERED_COMMAND)
#define DC_NOTIFY_DELAY         (1<<BIT_NOTIFIED_COMMAND)
#define DC_HBUFFER_DELAY        (1<<BIT_TIME_RELATIVE_TO_BUFFER)
#define DC_MULTIPLE_DELAY       (1<<BIT_RESERVED)
#define DC_STREAM_TIME_DELAY    (1<<BIT_STREAM_TIME)
#define DC_CANCELLED_DELAY      (1<<BIT_CANCELLED_COMMAND)

/* Values for tiDelayed field in TIME_INFO structure,
 * and for pbPause field in PLAY_BUFFER_INFO structure
 */
#define BIT_DIFFERED_COMMAND                0
#define BIT_NOTIFIED_COMMAND                1
#define BIT_TIME_RELATIVE_TO_BUFFER         2
#define BIT_RESERVED                        3
#define BIT_STREAM_TIME                     4
#define BIT_CANCELLED_COMMAND               5

/* Access to the "Size" field of the response of the CMD_GET_NOTIFY_EVENT request. */
#define GET_NOTIFY_EVENT_SIZE_FIELD_MASK    0x000000ff

/* DSP commands general masks */
#define OPCODE_MASK                 0x00ff0000
#define DSP_DIFFERED_COMMAND_MASK   0x0000C000

/* Notifications (NOTIFY_INFO) */
#define ALL_CMDS_NOTIFIED                   0x0000  // reserved
#define START_STREAM_NOTIFIED               0x0001
#define PAUSE_STREAM_NOTIFIED               0x0002
#define OUT_STREAM_LEVEL_NOTIFIED           0x0003
#define OUT_STREAM_PARAMETER_NOTIFIED       0x0004  // left for backward compatibility
#define OUT_STREAM_FORMAT_NOTIFIED          0x0004
#define PIPE_TIME_NOTIFIED                  0x0005
#define OUT_AUDIO_LEVEL_NOTIFIED            0x0006
#define OUT_STREAM_LEVEL_CURVE_NOTIFIED     0x0007
#define STREAM_TIME_NOTIFIED                0x0008
#define OUT_STREAM_EXTRAPARAMETER_NOTIFIED  0x0009
#define UNKNOWN_COMMAND_NOTIFIED            0xffff

/* Output pipe parameters setting */
#define MASK_VALID_PIPE_MPEG_PARAM      0x000040
#define MASK_VALID_PIPE_BACKWARD_PARAM  0x000020
#define MASK_SET_PIPE_MPEG_PARAM        0x000002
#define MASK_SET_PIPE_BACKWARD_PARAM    0x000001

#define MASK_DSP_WORD           0x00FFFFFF
#define MASK_ALL_STREAM         0x00FFFFFF
#define MASK_DSP_WORD_LEVEL     0x000001FF
#define MASK_FIRST_FIELD        0x0000001F
#define FIELD_SIZE              5

#define COMMAND_RECORD_MASK     0x000800

/* PipeManagement definition bits (PIPE_DECL_INFO) */
#define P_UNDERRUN_SKIP_SOUND_MASK				0x01
#define P_PREPARE_FOR_MPEG3_MASK				0x02
#define P_DO_NOT_RESET_ANALOG_LEVELS			0x04
#define P_ALLOW_UNDER_ALLOCATION_MASK			0x08
#define P_DATA_MODE_MASK				0x10
#define P_ASIO_BUFFER_MANAGEMENT_MASK			0x20

#define BIT_SKIP_SOUND					0x08	// bit 3
#define BIT_DATA_MODE					0x10	// bit 4
    
/* Bits in the CMD_MODIFY_CLOCK request. */
#define CMD_MODIFY_CLOCK_FD_BIT     0x00000001
#define CMD_MODIFY_CLOCK_T_BIT      0x00000002
#define CMD_MODIFY_CLOCK_S_BIT      0x00000004

/* Access to the results of the CMD_GET_TIME_CODE RMH. */
#define TIME_CODE_V_MASK            0x00800000
#define TIME_CODE_N_MASK            0x00400000
#define TIME_CODE_B_MASK            0x00200000
#define TIME_CODE_W_MASK            0x00100000

/* Values for the CMD_MANAGE_SIGNAL RMH. */
#define MANAGE_SIGNAL_TIME_CODE     0x01
#define MANAGE_SIGNAL_MIDI          0x02

/* Values for the CMD_CONFIG_TIME_CODE RMH. */
#define CONFIG_TIME_CODE_CANCEL     0x00001000
    
/* Mask to get only the effective time from the
 * high word out of the 2 returned by the DSP
 */
#define PCX_TIME_HI_MASK        0x000fffff

/* Values for setting a H-Buffer time */
#define HBUFFER_TIME_HIGH       0x00200000
#define HBUFFER_TIME_LOW        0x00000000

#define NOTIFY_MASK_TIME_HIGH   0x00400000
#define MULTIPLE_MASK_TIME_HIGH 0x00100000
#define STREAM_MASK_TIME_HIGH   0x00800000


/*
 *
 */
void vx_init_rmh(struct vx_rmh *rmh, unsigned int cmd);

/**
 * vx_send_pipe_cmd_params - fill first command word for pipe commands
 * @rmh: the rmh to be modified
 * @is_capture: 0 = playback, 1 = capture operation
 * @param1: first pipe-parameter
 * @param2: second pipe-parameter
 */
static inline void vx_set_pipe_cmd_params(struct vx_rmh *rmh, int is_capture,
					  int param1, int param2)
{
	if (is_capture)
		rmh->Cmd[0] |= COMMAND_RECORD_MASK;
	rmh->Cmd[0] |= (((u32)param1 & MASK_FIRST_FIELD) << FIELD_SIZE) & MASK_DSP_WORD;
		
	if (param2)
		rmh->Cmd[0] |= ((u32)param2 & MASK_FIRST_FIELD) & MASK_DSP_WORD;
	
}

/**
 * vx_set_stream_cmd_params - fill first command word for stream commands
 * @rmh: the rmh to be modified
 * @is_capture: 0 = playback, 1 = capture operation
 * @pipe: the pipe index (zero-based)
 */
static inline void vx_set_stream_cmd_params(struct vx_rmh *rmh, int is_capture, int pipe)
{
	if (is_capture)
		rmh->Cmd[0] |= COMMAND_RECORD_MASK;
	rmh->Cmd[0] |= (((u32)pipe & MASK_FIRST_FIELD) << FIELD_SIZE) & MASK_DSP_WORD;
}

#endif /* __VX_CMD_H */
