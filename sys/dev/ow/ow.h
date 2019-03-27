/*-
 * Copyright (c) 2015 M. Warner Losh <imp@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef DEV_OW_OW_H
#define DEV_OW_OW_H 1

enum ow_device_ivars {
	OW_IVAR_FAMILY,
	OW_IVAR_ROMID
};

#define OW_ACCESSOR(var, ivar, type)					\
	__BUS_ACCESSOR(ow, var, OW, ivar, type);
	
OW_ACCESSOR(family,	FAMILY,	uint8_t)
OW_ACCESSOR(romid,	ROMID, uint8_t *)

#undef OW_ACCSSOR

/*
 * The following likely should be in the own.h file, but needs to be here to
 * avoid recursive issues when defining the own_if.m interface.
 */

/*
 * Generalized command structure for a 1wire bus transaction. Not all possible
 * transactions on the 1wire bus can be represented here (a notable exception
 * being both the search ROM commands), but most of them can be, allowing for
 * general transactions from userland. A lower-level interface to the link
 * layer is also provided.
 */
#define MAX_ROM		10
#define MAX_XPT		32
#define MAX_READ	32
struct ow_cmd 
{
	uint32_t	flags;		/* Various flags */
#define OW_FLAG_OVERDRIVE	1	/* Send xpt stuff overdrive speed */
#define OW_FLAG_READ_BIT	2	/* Read a single bit after xpt_cmd */
	uint8_t		rom_len;	/* Number of ROM bytes to send */
	uint8_t		rom_cmd[MAX_ROM]; /* Rom command to send */
	uint8_t		rom_read_len;	/* Number of bytes to read */
	uint8_t		rom_read[MAX_ROM]; /* Extra bytes read */
	uint8_t		xpt_len;	/* Total transport bytes to send */
	uint8_t		xpt_cmd[MAX_XPT]; /* Device specific command to send, if flagged */
	uint8_t		xpt_read_len;	/* Number of bytes to read after */
	uint8_t		xpt_read[MAX_READ]; /* Buffer for read bytes */
};

typedef uint64_t romid_t;

#endif /* DEV_OW_OW_H */
