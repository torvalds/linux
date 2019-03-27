/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1995 Sean Eric Fagan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _IC_ESP_H_
#define	_IC_ESP_H_

/*
 * Definitions for Hayes ESP serial cards.
 */

/*
 * CMD1 and CMD2 are the command ports, offsets from <esp_iobase>.
 */
#define	ESP_CMD1	4
#define	ESP_CMD2	5

/*
 * STAT1 and STAT2 are to get return values and status bytes;
 * they overload CMD1 and CMD2.
 */
#define	ESP_STATUS1	ESP_CMD1
#define	ESP_STATUS2	ESP_CMD2

/*
 * Commands.  Commands are given by writing the command value to
 * ESP_CMD1 and then writing or reading some number of bytes from
 * ESP_CMD2 or ESP_STATUS2.
 */
#define	ESP_GETTEST	0x01	/* self-test command (1 byte + extras) */
#define	ESP_GETDIPS	0x02	/* get on-board DIP switches (1 byte) */
#define	ESP_SETFLOWTYPE	0x08	/* set type of flow-control (2 bytes) */
#define	ESP_SETRXFLOW	0x0a	/* set Rx FIFO flow control levels (4 bytes) */
#define	ESP_SETMODE	0x10	/* set board mode (1 byte) */
#define	ESP_SETCLOCK	0x23	/* set UART clock prescaler */

/* Mode bits (ESP_SETMODE). */
#define	ESP_MODE_FIFO	0x02	/* act like a 16550 (compatibility mode) */
#define	ESP_MODE_RTS	0x04	/* use RTS hardware flow control */
#define	ESP_MODE_SCALE	0x80	/* scale FIFO trigger levels */

/* Flow control type bits (ESP_SETFLOWTYPE). */
#define	ESP_FLOW_RTS	0x04	/* cmd1: local Rx sends RTS flow control */
#define	ESP_FLOW_CTS	0x10	/* cmd2: local transmitter responds to CTS */

/* Used by ESP_SETRXFLOW. */
#define	HIBYTE(w)	(((w) >> 8) & 0xff)
#define	LOBYTE(w)	((w) & 0xff)

#endif /* !_IC_ESP_H_ */
