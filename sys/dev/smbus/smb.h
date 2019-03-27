/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Nicolas Souchu
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
#ifndef __SMB_H
#define __SMB_H

#include <sys/ioccom.h>

struct smbcmd {
	u_char cmd;
	u_char reserved;
	u_short op;
	union {
		char	byte;
		char	buf[2];
		short	word;
	} wdata;
	union {
		char	byte;
		char	buf[2];
		short	word;
	} rdata;
	int  slave;
	char *wbuf;	/* use wdata if NULL */
	int  wcount;
	char *rbuf;	/* use rdata if NULL */
	int  rcount;
};

/*
 * SMBus spec 2.0 says block transfers may be at most 32 bytes.
 */
#define SMB_MAXBLOCKSIZE	32

#define SMB_QUICK_WRITE	_IOW('i', 1, struct smbcmd)
#define SMB_QUICK_READ	_IOW('i', 2, struct smbcmd)
#define SMB_SENDB	_IOW('i', 3, struct smbcmd)
#define SMB_RECVB	_IOWR('i', 4, struct smbcmd)
#define SMB_WRITEB	_IOW('i', 5, struct smbcmd)
#define SMB_WRITEW	_IOW('i', 6, struct smbcmd)
#define SMB_READB	_IOWR('i', 7, struct smbcmd)
#define SMB_READW	_IOWR('i', 8, struct smbcmd)
#define SMB_PCALL	_IOWR('i', 9, struct smbcmd)
#define SMB_BWRITE	_IOW('i', 10, struct smbcmd)
#define SMB_BREAD	_IOWR('i', 11, struct smbcmd)
#define SMB_OLD_TRANS	_IOWR('i', 12, struct smbcmd)

#endif
