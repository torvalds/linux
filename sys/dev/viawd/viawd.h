/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Fabien Thomas <fabient@FreeBSD.org>
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

#ifndef _VIAWD_H_
#define _VIAWD_H_

struct viawd_device {
	uint16_t		device;
	char			*desc;
};

struct viawd_softc {
	device_t		dev;
	device_t		sb_dev;

	int			wd_rid;
	struct resource		*wd_res;

	eventhandler_tag	ev_tag;
	unsigned int		timeout;
};

#define VENDORID_VIA		0x1106
#define DEVICEID_VT8251		0x3287
#define DEVICEID_CX700		0x8324
#define DEVICEID_VX800		0x8353
#define DEVICEID_VX855		0x8409
#define DEVICEID_VX900		0x8410

#define VIAWD_CONFIG_BASE	0xE8

#define VIAWD_MEM_LEN		8

#define VIAWD_MEM_CTRL		0x00
#define VIAWD_MEM_CTRL_TRIGGER	0x000000080
#define VIAWD_MEM_CTRL_DISABLE	0x000000008
#define VIAWD_MEM_CTRL_POWEROFF	0x000000004
#define VIAWD_MEM_CTRL_FIRED	0x000000002
#define VIAWD_MEM_CTRL_ENABLE	0x000000001

#define VIAWD_MEM_COUNT		0x04

#define VIAWD_MEM_COUNT_MIN	1
#define VIAWD_MEM_COUNT_MAX	1023

#define VIAWD_TIMEOUT_SHUTDOWN	(5 * 60)

#endif
