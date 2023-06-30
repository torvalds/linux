/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LIBPS2_H
#define _LIBPS2_H

/*
 * Copyright (C) 1999-2002 Vojtech Pavlik
 * Copyright (C) 2004 Dmitry Torokhov
 */

#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/wait.h>

struct ps2dev;

/**
 * enum ps2_disposition - indicates how received byte should be handled
 * @PS2_PROCESS: pass to the main protocol handler, process normally
 * @PS2_IGNORE: skip the byte
 * @PS2_ERROR: do not process the byte, abort command in progress
 */
enum ps2_disposition {
	PS2_PROCESS,
	PS2_IGNORE,
	PS2_ERROR,
};

typedef enum ps2_disposition (*ps2_pre_receive_handler_t)(struct ps2dev *, u8,
							  unsigned int);
typedef void (*ps2_receive_handler_t)(struct ps2dev *, u8);

/**
 * struct ps2dev - represents a device using PS/2 protocol
 * @serio: a serio port used by the PS/2 device
 * @cmd_mutex: a mutex ensuring that only one command is executing at a time
 * @wait: a waitqueue used to signal completion from the serio interrupt handler
 * @flags: various internal flags indicating stages of PS/2 command execution
 * @cmdbuf: buffer holding command response
 * @cmdcnt: outstanding number of bytes of the command response
 * @nak: a byte transmitted by the device when it refuses command
 * @pre_receive_handler: checks communication errors and returns disposition
 * (&enum ps2_disposition) of the received data byte
 * @receive_handler: main handler of particular PS/2 protocol, such as keyboard
 *   or mouse protocol
 */
struct ps2dev {
	struct serio *serio;
	struct mutex cmd_mutex;
	wait_queue_head_t wait;
	unsigned long flags;
	u8 cmdbuf[8];
	u8 cmdcnt;
	u8 nak;

	ps2_pre_receive_handler_t pre_receive_handler;
	ps2_receive_handler_t receive_handler;
};

void ps2_init(struct ps2dev *ps2dev, struct serio *serio,
	      ps2_pre_receive_handler_t pre_receive_handler,
	      ps2_receive_handler_t receive_handler);
int ps2_sendbyte(struct ps2dev *ps2dev, u8 byte, unsigned int timeout);
void ps2_drain(struct ps2dev *ps2dev, size_t maxbytes, unsigned int timeout);
void ps2_begin_command(struct ps2dev *ps2dev);
void ps2_end_command(struct ps2dev *ps2dev);
int __ps2_command(struct ps2dev *ps2dev, u8 *param, unsigned int command);
int ps2_command(struct ps2dev *ps2dev, u8 *param, unsigned int command);
int ps2_sliced_command(struct ps2dev *ps2dev, u8 command);
bool ps2_is_keyboard_id(u8 id);

irqreturn_t ps2_interrupt(struct serio *serio, u8 data, unsigned int flags);

#endif /* _LIBPS2_H */
