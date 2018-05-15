/*
 * tdf8532.h - Codec driver for NXP Semiconductors
 * Copyright (c) 2017, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */


#ifndef __TDF8532_H_
#define __TDF8532_H_

#define ACK_TIMEOUT 300

#define CHNL_MAX 5

#define AMP_MOD 0x80
#define END -1

#define MSG_TYPE_STX 0x02
#define MSG_TYPE_NAK 0x15
#define MSG_TYPE_ACK 0x6

#define HEADER_SIZE 3
#define HEADER_TYPE 0
#define HEADER_PKTID 1
#define HEADER_LEN 2

/* Set commands */
#define SET_CLK_STATE 0x1A
#define CLK_DISCONNECT 0x00
#define CLK_CONNECT 0x01

#define SET_CHNL_ENABLE 0x26
#define SET_CHNL_DISABLE 0x27

#define SET_CHNL_MUTE 0x42
#define SET_CHNL_UNMUTE 0x43

struct header_repl {
	u8 msg_type;
	u8 pkt_id;
	u8 len;
} __packed;

#define GET_IDENT 0xE0

struct get_ident_repl {
	struct header_repl header;
	u8 module_id;
	u8 cmd_id;
	u8 type_name;
	u8 hw_major;
	u8 hw_minor;
	u8 sw_major;
	u8 sw_minor;
	u8 sw_sub;
} __packed;

#define GET_ERROR 0xE2

struct get_error_repl {
	struct header_repl header;
	u8 module_id;
	u8 cmd_id;
	u8 last_cmd_id;
	u8 error;
	u8 status;
} __packed;

#define GET_DEV_STATUS 0x80

enum dev_state {STATE_BOOT, STATE_IDLE, STATE_STBY, STATE_LDAG, STATE_PLAY,
			STATE_PROT, STATE_SDWN, STATE_CLFA, STATE_NONE };

struct get_dev_status_repl {
	struct header_repl header;
	u8 module_id;
	u8 cmd_id;
	u8 state;
} __packed;

/* Helpers */
#define CHNL_MASK(channels) (u8)((0x00FF << channels) >> 8)

#define tdf8532_amp_write(dev_data, ...)\
	__tdf8532_single_write(dev_data, 0, AMP_MOD, __VA_ARGS__, END)

struct tdf8532_priv {
	struct i2c_client *i2c;
	u8 channels;
	u8 pkt_id;
};

#endif
