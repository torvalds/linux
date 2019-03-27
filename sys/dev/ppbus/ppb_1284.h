/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Nicolas Souchu
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
 *
 */
#ifndef __1284_H
#define __1284_H

/*
 * IEEE1284 signals
 */

/* host driven signals */

#define nHostClk	STROBE
#define Write		STROBE

#define nHostBusy	AUTOFEED
#define nHostAck	AUTOFEED
#define DStrb		AUTOFEED

#define nReveseRequest	nINIT

#define nActive1284	SELECTIN
#define AStrb		SELECTIN

/* peripheral driven signals */

#define nDataAvail	nFAULT
#define nPeriphRequest	nFAULT

#define Xflag		SELECT

#define AckDataReq	PERROR
#define nAckReverse	PERROR

#define nPtrBusy	nBUSY
#define nPeriphAck	nBUSY
#define Wait		nBUSY

#define PtrClk		nACK
#define PeriphClk	nACK
#define Intr		nACK

/* request mode values */
#define NIBBLE_1284_NORMAL	0x0
#define NIBBLE_1284_REQUEST_ID	0x4
#define BYTE_1284_NORMAL	0x1
#define BYTE_1284_REQUEST_ID	0x5
#define ECP_1284_NORMAL		0x10
#define ECP_1284_REQUEST_ID	0x14
#define ECP_1284_RLE		0x30
#define ECP_1284_RLE_REQUEST_ID	0x34
#define EPP_1284_NORMAL		0x40
#define EXT_LINK_1284_NORMAL	0x80

/* ieee1284 mode options */
#define PPB_REQUEST_ID		0x1
#define PPB_USE_RLE		0x2
#define PPB_EXTENSIBILITY_LINK	0x4

/* ieee1284 errors */
#define PPB_NO_ERROR		0
#define PPB_MODE_UNSUPPORTED	1	/* mode not supported by peripheral */
#define PPB_NOT_IEEE1284	2	/* not an IEEE1284 compliant periph. */
#define PPB_TIMEOUT		3	/* timeout */
#define PPB_INVALID_MODE	4	/* current mode is incorrect */

/* ieee1284 host side states */
#define PPB_ERROR			0
#define PPB_FORWARD_IDLE		1
#define PPB_NEGOCIATION			2
#define PPB_SETUP			3
#define PPB_ECP_FORWARD_IDLE		4
#define PPB_FWD_TO_REVERSE		5
#define PPB_REVERSE_IDLE		6
#define PPB_REVERSE_TRANSFER		7
#define PPB_REVERSE_TO_FWD		8
#define PPB_EPP_IDLE			9
#define PPB_TERMINATION			10

/* peripheral side states */
#define PPB_PERIPHERAL_NEGOCIATION	11
#define PPB_PERIPHERAL_IDLE		12
#define PPB_PERIPHERAL_TRANSFER		13
#define PPB_PERIPHERAL_TERMINATION	14

extern int nibble_1284_inbyte(device_t, char *);
extern int byte_1284_inbyte(device_t, char *);
extern int spp_1284_read(device_t, int, char *, int, int *);

extern int ppb_1284_negociate(device_t, int, int);
extern int ppb_1284_terminate(device_t);
extern int ppb_1284_read_id(device_t, int, char *, int, int *);
extern int ppb_1284_read(device_t, int, char *, int, int *);
extern int ppb_1284_get_state(device_t bus);
extern int ppb_1284_set_state(device_t bus, int state);

extern int ppb_peripheral_terminate(device_t, int);
extern int ppb_peripheral_negociate(device_t, int, int);
extern int byte_peripheral_write(device_t, char *, int, int *);

#endif
