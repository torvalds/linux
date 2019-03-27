/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2010 Nathan Whitehorn
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

#ifndef _POWERPC_PS3_PS3BUS_H
#define _POWERPC_PS3_PS3BUS_H

enum {
	PS3BUS_IVAR_BUS,
	PS3BUS_IVAR_DEVICE,
	PS3BUS_IVAR_BUSTYPE,
	PS3BUS_IVAR_DEVTYPE,
	PS3BUS_IVAR_BUSIDX,
	PS3BUS_IVAR_DEVIDX,
};

#define PS3BUS_ACCESSOR(A, B, T) \
	__BUS_ACCESSOR(ps3bus, A, PS3BUS, B, T)

PS3BUS_ACCESSOR(bus,		BUS,		int)
PS3BUS_ACCESSOR(device,		DEVICE,		int)
PS3BUS_ACCESSOR(bustype,	BUSTYPE,	uint64_t)
PS3BUS_ACCESSOR(devtype,	DEVTYPE,	uint64_t)
PS3BUS_ACCESSOR(busidx,		BUSIDX,		int)
PS3BUS_ACCESSOR(devidx,		DEVIDX,		int)

/* Bus types */
enum {
	PS3_BUSTYPE_SYSBUS = 4,
	PS3_BUSTYPE_STORAGE = 5,
};

/* Device types */
enum {
	/* System bus devices */
	PS3_DEVTYPE_GELIC = 3,
	PS3_DEVTYPE_USB = 4,
	PS3_DEVTYPE_GPIO = 6,

	/* Storage bus devices */
	PS3_DEVTYPE_DISK = 0,
	PS3_DEVTYPE_CDROM = 5,
	PS3_DEVTYPE_FLASH = 14,
};

#endif /* _POWERPC_PS3_PS3BUS_H */
