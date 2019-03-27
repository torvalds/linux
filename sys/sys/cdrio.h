/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2000,2001,2002 Søren Schmidt <sos@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#ifndef	_SYS_CDRIO_H_
#define	_SYS_CDRIO_H_

#include <sys/ioccom.h>

struct cdr_track {
        int datablock_type;         	/* data type code */
#define CDR_DB_RAW              0x0     /* 2352 bytes of raw data */
#define CDR_DB_RAW_PQ           0x1     /* 2368 bytes raw data + P/Q subchan */
#define CDR_DB_RAW_PW           0x2     /* 2448 bytes raw data + P-W subchan */
#define CDR_DB_RAW_PW_R         0x3     /* 2448 bytes raw data + P-W raw sub */
#define CDR_DB_RES_4            0x4     /* reserved */
#define CDR_DB_RES_5            0x5     /* reserved */
#define CDR_DB_RES_6            0x6     /* reserved */
#define CDR_DB_VS_7             0x7     /* vendor specific */
#define CDR_DB_ROM_MODE1        0x8     /* 2048 bytes Mode 1 (ISO/IEC 10149) */
#define CDR_DB_ROM_MODE2        0x9     /* 2336 bytes Mode 2 (ISO/IEC 10149) */
#define CDR_DB_XA_MODE1         0xa     /* 2048 bytes Mode 1 (CD-ROM XA 1) */
#define CDR_DB_XA_MODE2_F1      0xb     /* 2056 bytes Mode 2 (CD-ROM XA 1) */
#define CDR_DB_XA_MODE2_F2      0xc     /* 2324 bytes Mode 2 (CD-ROM XA 2) */
#define CDR_DB_XA_MODE2_MIX     0xd     /* 2332 bytes Mode 2 (CD-ROM XA 1/2) */
#define CDR_DB_RES_14           0xe     /* reserved */
#define CDR_DB_VS_15            0xf     /* vendor specific */

	int preemp;			/* preemphasis if audio track*/
	int test_write;			/* use test writes, laser turned off */
};

struct cdr_cue_entry {
	u_int8_t adr:4;
    	u_int8_t ctl:4;
	u_int8_t track;
	u_int8_t index;
	u_int8_t dataform;
	u_int8_t scms;
	u_int8_t min;
	u_int8_t sec;
	u_int8_t frame;
};

struct cdr_cuesheet {
    	int32_t len;
	struct cdr_cue_entry *entries;
	int session_format;
#define CDR_SESS_CDROM          0x00
#define CDR_SESS_CDI            0x10
#define CDR_SESS_CDROM_XA       0x20

	int session_type;
#define CDR_SESS_NONE           0x00
#define CDR_SESS_FINAL          0x01
#define CDR_SESS_RESERVED       0x02
#define CDR_SESS_MULTI          0x03

	int test_write;
};

struct cdr_format_capacity {
	u_int32_t blocks;
	u_int32_t reserved:2;
	u_int32_t type:6;
	u_int32_t param:24;
};

struct cdr_format_capacities {
	u_int8_t reserved1[3];
	u_int8_t length;
	u_int32_t blocks;
	u_int32_t type:2;
	u_int32_t reserved2:6;
	u_int32_t block_size:24;
	struct cdr_format_capacity format[32];
};

struct cdr_format_params {
	u_int8_t reserved;
	u_int8_t vs:1;
	u_int8_t immed:1;
	u_int8_t try_out:1;
	u_int8_t ip:1;
	u_int8_t stpf:1;
	u_int8_t dcrt:1;
	u_int8_t dpry:1;
	u_int8_t fov:1;
	u_int16_t length;
	struct cdr_format_capacity format;
};

#define CDRIOCBLANK		_IOW('c', 100, int)
#define CDR_B_ALL		0x0
#define CDR_B_MIN		0x1
#define CDR_B_SESSION		0x6

#define CDRIOCNEXTWRITEABLEADDR	_IOR('c', 101, int)
#define CDRIOCINITWRITER	_IOW('c', 102, int)
#define CDRIOCINITTRACK		_IOW('c', 103, struct cdr_track)
#define CDRIOCSENDCUE		_IOW('c', 104, struct cdr_cuesheet)
#define CDRIOCFLUSH		_IO('c', 105)
#define CDRIOCFIXATE		_IOW('c', 106, int)
#define CDRIOCREADSPEED		_IOW('c', 107, int)
#define CDRIOCWRITESPEED	_IOW('c', 108, int)
#define CDR_MAX_SPEED		0xffff
#define CDRIOCGETBLOCKSIZE	_IOR('c', 109, int)
#define CDRIOCSETBLOCKSIZE	_IOW('c', 110, int)
#define CDRIOCGETPROGRESS	_IOR('c', 111, int)
#define CDRIOCREADFORMATCAPS	_IOR('c', 112, struct cdr_format_capacities)
#define CDRIOCFORMAT		_IOW('c', 113, struct cdr_format_params)

#endif /* !_SYS_CDRIO_H_ */
