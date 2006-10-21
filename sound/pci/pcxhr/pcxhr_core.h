/*
 * Driver for Digigram pcxhr compatible soundcards
 *
 * low level interface with interrupt ans message handling
 *
 * Copyright (c) 2004 by Digigram <alsa@digigram.com>
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

#ifndef __SOUND_PCXHR_CORE_H
#define __SOUND_PCXHR_CORE_H

struct firmware;
struct pcxhr_mgr;

/* init and firmware download commands */
void pcxhr_reset_xilinx_com(struct pcxhr_mgr *mgr);
void pcxhr_reset_dsp(struct pcxhr_mgr *mgr);
void pcxhr_enable_dsp(struct pcxhr_mgr *mgr);
int pcxhr_load_xilinx_binary(struct pcxhr_mgr *mgr, const struct firmware *xilinx, int second);
int pcxhr_load_eeprom_binary(struct pcxhr_mgr *mgr, const struct firmware *eeprom);
int pcxhr_load_boot_binary(struct pcxhr_mgr *mgr, const struct firmware *boot);
int pcxhr_load_dsp_binary(struct pcxhr_mgr *mgr, const struct firmware *dsp);

/* DSP time available on MailBox4 register : 24 bit time samples() */
#define PCXHR_DSP_TIME_MASK		0x00ffffff
#define PCXHR_DSP_TIME_INVALID		0x10000000


#define PCXHR_SIZE_MAX_CMD		8
#define PCXHR_SIZE_MAX_STATUS		16
#define PCXHR_SIZE_MAX_LONG_STATUS	256

struct pcxhr_rmh {
	u16	cmd_len;		/* length of the command to send (WORDs) */
	u16	stat_len;		/* length of the status received (WORDs) */
	u16	dsp_stat;		/* status type, RMP_SSIZE_XXX */
	u16	cmd_idx;		/* index of the command */
	u32	cmd[PCXHR_SIZE_MAX_CMD];
	u32	stat[PCXHR_SIZE_MAX_STATUS];
};

enum {
	CMD_VERSION,			/* cmd_len = 2	stat_len = 1 */
	CMD_SUPPORTED,			/* cmd_len = 1	stat_len = 4 */
	CMD_TEST_IT,			/* cmd_len = 1	stat_len = 1 */
	CMD_SEND_IRQA,			/* cmd_len = 1	stat_len = 0 */
	CMD_ACCESS_IO_WRITE,		/* cmd_len >= 1	stat_len >= 1 */
	CMD_ACCESS_IO_READ,		/* cmd_len >= 1	stat_len >= 1 */
	CMD_ASYNC,			/* cmd_len = 1	stat_len = 1 */
	CMD_MODIFY_CLOCK,		/* cmd_len = 3	stat_len = 0 */
	CMD_RESYNC_AUDIO_INPUTS,	/* cmd_len = 1	stat_len = 0 */
	CMD_GET_DSP_RESOURCES,		/* cmd_len = 1	stat_len = 4 */
	CMD_SET_TIMER_INTERRUPT,	/* cmd_len = 1	stat_len = 0 */
	CMD_RES_PIPE,			/* cmd_len = 2	stat_len = 0 */
	CMD_FREE_PIPE,			/* cmd_len = 1	stat_len = 0 */
	CMD_CONF_PIPE,			/* cmd_len = 2	stat_len = 0 */
	CMD_STOP_PIPE,			/* cmd_len = 1	stat_len = 0 */
	CMD_PIPE_SAMPLE_COUNT,		/* cmd_len = 2	stat_len = 2 */
	CMD_CAN_START_PIPE,		/* cmd_len >= 1	stat_len = 1 */
	CMD_START_STREAM,		/* cmd_len = 2	stat_len = 0 */
	CMD_STREAM_OUT_LEVEL_ADJUST,	/* cmd_len >= 1	stat_len = 0 */
	CMD_STOP_STREAM,		/* cmd_len = 2	stat_len = 0 */
	CMD_UPDATE_R_BUFFERS,		/* cmd_len = 4	stat_len = 0 */
	CMD_FORMAT_STREAM_OUT,		/* cmd_len >= 2	stat_len = 0 */
	CMD_FORMAT_STREAM_IN,		/* cmd_len >= 4	stat_len = 0 */
	CMD_STREAM_SAMPLE_COUNT,	/* cmd_len = 2	stat_len = (2 * nb_stream) */
	CMD_AUDIO_LEVEL_ADJUST,		/* cmd_len = 3	stat_len = 0 */
	CMD_LAST_INDEX
};

#define MASK_DSP_WORD		0x00ffffff
#define MASK_ALL_STREAM		0x00ffffff
#define MASK_DSP_WORD_LEVEL	0x000001ff
#define MASK_FIRST_FIELD	0x0000001f
#define FIELD_SIZE		5

/*
 init the rmh struct; by default cmd_len is set to 1
 */
void pcxhr_init_rmh(struct pcxhr_rmh *rmh, int cmd);

void pcxhr_set_pipe_cmd_params(struct pcxhr_rmh* rmh, int capture, unsigned int param1,
			       unsigned int param2, unsigned int param3);

/*
 send the rmh
 */
int pcxhr_send_msg(struct pcxhr_mgr *mgr, struct pcxhr_rmh *rmh);


/* values used for CMD_ACCESS_IO_WRITE and CMD_ACCESS_IO_READ */
#define IO_NUM_REG_CONT			0
#define IO_NUM_REG_GENCLK		1
#define IO_NUM_REG_MUTE_OUT		2
#define IO_NUM_SPEED_RATIO		4
#define IO_NUM_REG_STATUS		5
#define IO_NUM_REG_CUER			10
#define IO_NUM_UER_CHIP_REG		11
#define IO_NUM_REG_OUT_ANA_LEVEL	20
#define IO_NUM_REG_IN_ANA_LEVEL		21


#define REG_CONT_UNMUTE_INPUTS		0x020000

/* parameters used with register IO_NUM_REG_STATUS */
#define REG_STATUS_OPTIONS		0
#define REG_STATUS_AES_SYNC		8
#define REG_STATUS_AES_1		9
#define REG_STATUS_AES_2		10
#define REG_STATUS_AES_3		11
#define REG_STATUS_AES_4		12
#define REG_STATUS_WORD_CLOCK		13
#define REG_STATUS_INTER_SYNC		14
#define REG_STATUS_CURRENT		0x80
/* results */
#define REG_STATUS_OPT_NO_VIDEO_SIGNAL	0x01
#define REG_STATUS_OPT_DAUGHTER_MASK	0x1c
#define REG_STATUS_OPT_ANALOG_BOARD	0x00
#define REG_STATUS_OPT_NO_DAUGHTER	0x1c
#define REG_STATUS_OPT_COMPANION_MASK	0xe0
#define REG_STATUS_OPT_NO_COMPANION	0xe0
#define REG_STATUS_SYNC_32000		0x00
#define REG_STATUS_SYNC_44100		0x01
#define REG_STATUS_SYNC_48000		0x02
#define REG_STATUS_SYNC_64000		0x03
#define REG_STATUS_SYNC_88200		0x04
#define REG_STATUS_SYNC_96000		0x05
#define REG_STATUS_SYNC_128000		0x06
#define REG_STATUS_SYNC_176400		0x07
#define REG_STATUS_SYNC_192000		0x08

int pcxhr_set_pipe_state(struct pcxhr_mgr *mgr, int playback_mask, int capture_mask, int start);

int pcxhr_write_io_num_reg_cont(struct pcxhr_mgr *mgr, unsigned int mask,
				unsigned int value, int *changed);

/* codec parameters */
#define CS8416_RUN		0x200401
#define CS8416_FORMAT_DETECT	0x200b00
#define CS8416_CSB0		0x201900
#define CS8416_CSB1		0x201a00
#define CS8416_CSB2		0x201b00
#define CS8416_CSB3		0x201c00
#define CS8416_CSB4		0x201d00
#define CS8416_VERSION		0x207f00

#define CS8420_DATA_FLOW_CTL	0x200301
#define CS8420_CLOCK_SRC_CTL	0x200401
#define CS8420_RECEIVER_ERRORS	0x201000
#define CS8420_SRC_RATIO	0x201e00
#define CS8420_CSB0		0x202000
#define CS8420_CSB1		0x202100
#define CS8420_CSB2		0x202200
#define CS8420_CSB3		0x202300
#define CS8420_CSB4		0x202400
#define CS8420_VERSION		0x207f00

#define CS4271_MODE_CTL_1	0x200101
#define CS4271_DAC_CTL		0x200201
#define CS4271_VOLMIX		0x200301
#define CS4271_VOLMUTE_LEFT	0x200401
#define CS4271_VOLMUTE_RIGHT	0x200501
#define CS4271_ADC_CTL		0x200601
#define CS4271_MODE_CTL_2	0x200701

#define CHIP_SIG_AND_MAP_SPI	0xff7f00

/* codec selection */
#define CS4271_01_CS		0x160018
#define CS4271_23_CS		0x160019
#define CS4271_45_CS		0x16001a
#define CS4271_67_CS		0x16001b
#define CS4271_89_CS		0x16001c
#define CS4271_AB_CS		0x16001d
#define CS8420_01_CS		0x080090
#define CS8420_23_CS		0x080092
#define CS8420_45_CS		0x080094
#define CS8420_67_CS		0x080096
#define CS8416_01_CS		0x080098


/* interrupt handling */
irqreturn_t pcxhr_interrupt(int irq, void *dev_id);
void pcxhr_msg_tasklet(unsigned long arg);

#endif /* __SOUND_PCXHR_CORE_H */
