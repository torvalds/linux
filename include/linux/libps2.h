#ifndef _LIBPS2_H
#define _LIBPS2_H

/*
 * Copyright (C) 1999-2002 Vojtech Pavlik
 * Copyright (C) 2004 Dmitry Torokhov
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */


#define PS2_CMD_GETID		0x02f2
#define PS2_CMD_RESET_BAT	0x02ff

#define PS2_RET_BAT		0xaa
#define PS2_RET_ID		0x00
#define PS2_RET_ACK		0xfa
#define PS2_RET_NAK		0xfe

#define PS2_FLAG_ACK		1	/* Waiting for ACK/NAK */
#define PS2_FLAG_CMD		2	/* Waiting for command to finish */
#define PS2_FLAG_CMD1		4	/* Waiting for the first byte of command response */
#define PS2_FLAG_WAITID		8	/* Command execiting is GET ID */

struct ps2dev {
	struct serio *serio;

	/* Ensures that only one command is executing at a time */
	struct mutex cmd_mutex;

	/* Used to signal completion from interrupt handler */
	wait_queue_head_t wait;

	unsigned long flags;
	unsigned char cmdbuf[6];
	unsigned char cmdcnt;
	unsigned char nak;
};

void ps2_init(struct ps2dev *ps2dev, struct serio *serio);
int ps2_sendbyte(struct ps2dev *ps2dev, unsigned char byte, int timeout);
void ps2_drain(struct ps2dev *ps2dev, int maxbytes, int timeout);
int ps2_command(struct ps2dev *ps2dev, unsigned char *param, int command);
int ps2_handle_ack(struct ps2dev *ps2dev, unsigned char data);
int ps2_handle_response(struct ps2dev *ps2dev, unsigned char data);
void ps2_cmd_aborted(struct ps2dev *ps2dev);
int ps2_is_keyboard_id(char id);

#endif /* _LIBPS2_H */
