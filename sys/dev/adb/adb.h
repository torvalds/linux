/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2008 Nathan Whitehorn
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_POWERPC_ADB_H_
#define	_POWERPC_ADB_H_

#include "adb_hb_if.h"
#include "adb_if.h"

enum {
	ADB_COMMAND_FLUSH	= 0,
	ADB_COMMAND_LISTEN	= 2,
	ADB_COMMAND_TALK	= 3,
};

enum {
	ADB_DEVICE_DONGLE	= 0x01,
	ADB_DEVICE_KEYBOARD	= 0x02,
	ADB_DEVICE_MOUSE	= 0x03,
	ADB_DEVICE_TABLET	= 0x04,
	ADB_DEVICE_MODEM	= 0x05,
	
	ADB_DEVICE_MISC		= 0x07
};

struct adb_devinfo {
	uint8_t address;
	uint8_t default_address;
	uint8_t handler_id;

	uint16_t register3;
};

/* Pass packets down through the bus manager */
u_int adb_send_packet(device_t dev, u_char command, u_char reg, int len, 
    u_char *data);
u_int adb_set_autopoll(device_t dev, u_char enable);

/* Pass packets up from the interface */
u_int adb_receive_raw_packet(device_t dev, u_char status, u_char command, 
    int len, u_char *data);

uint8_t adb_get_device_type(device_t dev);
uint8_t adb_get_device_handler(device_t dev);
uint8_t adb_set_device_handler(device_t dev, uint8_t newhandler);

size_t	adb_read_register(device_t dev, u_char reg, void *data);
size_t	adb_write_register(device_t dev, u_char reg, size_t len, void *data);

/* Bits for implementing ADB host bus adapters */
extern devclass_t adb_devclass;
extern driver_t adb_driver;

#endif

