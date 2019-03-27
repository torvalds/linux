/* $FreeBSD$ */
/*-
 * Copyright (c) 2010-2012 Hans Petter Selasky. All rights reserved.
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
 */

#ifndef _CUSE_DEFS_H_
#define	_CUSE_DEFS_H_

#define	CUSE_VERSION		0x000123

#define	CUSE_ERR_NONE		0
#define	CUSE_ERR_BUSY		-1
#define	CUSE_ERR_WOULDBLOCK	-2
#define	CUSE_ERR_INVALID	-3
#define	CUSE_ERR_NO_MEMORY	-4
#define	CUSE_ERR_FAULT		-5
#define	CUSE_ERR_SIGNAL		-6
#define	CUSE_ERR_OTHER		-7
#define	CUSE_ERR_NOT_LOADED	-8
#define	CUSE_ERR_NO_DEVICE	-9

#define	CUSE_POLL_NONE		0
#define	CUSE_POLL_READ		1
#define	CUSE_POLL_WRITE		2
#define	CUSE_POLL_ERROR		4

#define	CUSE_FFLAG_NONE		0
#define	CUSE_FFLAG_READ		1
#define	CUSE_FFLAG_WRITE	2
#define	CUSE_FFLAG_NONBLOCK	4

#define	CUSE_DBG_NONE		0
#define	CUSE_DBG_FULL		1

/* maximum data transfer length */
#define	CUSE_LENGTH_MAX		0x7FFFFFFFU

enum {
	CUSE_CMD_NONE,
	CUSE_CMD_OPEN,
	CUSE_CMD_CLOSE,
	CUSE_CMD_READ,
	CUSE_CMD_WRITE,
	CUSE_CMD_IOCTL,
	CUSE_CMD_POLL,
	CUSE_CMD_SIGNAL,
	CUSE_CMD_SYNC,
	CUSE_CMD_MAX,
};

#define	CUSE_MAKE_ID(a,b,c,u) ((((a) & 0x7F) << 24)| \
    (((b) & 0xFF) << 16)|(((c) & 0xFF) << 8)|((u) & 0xFF))

#define	CUSE_ID_MASK 0x7FFFFF00U

/*
 * The following ID's are defined:
 * ===============================
 */
#define	CUSE_ID_DEFAULT(what) CUSE_MAKE_ID(0,0,what,0)
#define	CUSE_ID_WEBCAMD(what) CUSE_MAKE_ID('W','C',what,0)	/* Used by Webcamd. */
#define	CUSE_ID_SUNDTEK(what) CUSE_MAKE_ID('S','K',what,0)	/* Used by Sundtek. */
#define	CUSE_ID_CX88(what) CUSE_MAKE_ID('C','X',what,0)		/* Used by cx88 driver. */
#define	CUSE_ID_UHIDD(what) CUSE_MAKE_ID('U','D',what,0)	/* Used by uhidd. */

#endif					/* _CUSE_DEFS_H_ */
