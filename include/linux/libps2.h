/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LIBPS2_H
#define _LIBPS2_H

/*
 * Copyright (C) 1999-2002 Vojtech Pavlik
 * Copyright (C) 2004 Dmitry Torokhov
 */

#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/wait.h>

#define PS2_CMD_SETSCALE11	0x00e6
#define PS2_CMD_SETRES		0x10e8
#define PS2_CMD_GETID		0x02f2
#define PS2_CMD_RESET_BAT	0x02ff

#define PS2_RET_BAT		0xaa
#define PS2_RET_ID		0x00
#define PS2_RET_ACK		0xfa
#define PS2_RET_NAK		0xfe
#define PS2_RET_ERR		0xfc

#define PS2_FLAG_ACK		BIT(0)	/* Waiting for ACK/NAK */
#define PS2_FLAG_CMD		BIT(1)	/* Waiting for a command to finish */
#define PS2_FLAG_CMD1		BIT(2)	/* Waiting for the first byte of command response */
#define PS2_FLAG_WAITID		BIT(3)	/* Command executing is GET ID */
#define PS2_FLAG_NAK		BIT(4)	/* Last transmission was NAKed */
#define PS2_FLAG_ACK_CMD	BIT(5)	/* Waiting to ACK the command (first) byte */

struct ps2dev {
	struct serio *serio;

	/* Ensures that only one command is executing at a time */
	struct mutex cmd_mutex;

	/* Used to signal completion from interrupt handler */
	wait_queue_head_t wait;

	unsigned long flags;
	u8 cmdbuf[8];
	u8 cmdcnt;
	u8 nak;
};

void ps2_init(struct ps2dev *ps2dev, struct serio *serio);
int ps2_sendbyte(struct ps2dev *ps2dev, u8 byte, unsigned int timeout);
void ps2_drain(struct ps2dev *ps2dev, size_t maxbytes, unsigned int timeout);
void ps2_begin_command(struct ps2dev *ps2dev);
void ps2_end_command(struct ps2dev *ps2dev);
int __ps2_command(struct ps2dev *ps2dev, u8 *param, unsigned int command);
int ps2_command(struct ps2dev *ps2dev, u8 *param, unsigned int command);
int ps2_sliced_command(struct ps2dev *ps2dev, u8 command);
bool ps2_handle_ack(struct ps2dev *ps2dev, u8 data);
bool ps2_handle_response(struct ps2dev *ps2dev, u8 data);
void ps2_cmd_aborted(struct ps2dev *ps2dev);
bool ps2_is_keyboard_id(u8 id);

#endif /* _LIBPS2_H */
