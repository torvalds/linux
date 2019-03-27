/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999,2000,2001,2002 Søren Schmidt <sos@FreeBSD.org>
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

#ifndef _SYS_DVDIO_H_
#define _SYS_DVDIO_H_

struct dvd_layer {
	u_int8_t book_type	:4;
	u_int8_t book_version	:4;
	u_int8_t disc_size	:4;
	u_int8_t max_rate	:4;
	u_int8_t nlayers	:2;
	u_int8_t track_path	:1;
	u_int8_t layer_type	:4;
	u_int8_t linear_density	:4;
	u_int8_t track_density	:4;
	u_int8_t bca		:1;
	u_int32_t start_sector;
	u_int32_t end_sector;
	u_int32_t end_sector_l0;
};

struct dvd_struct {
	u_char format;
	u_char layer_num;
	u_char cpst;
	u_char rmi;
	u_int8_t agid		:2;
	u_int32_t length;
	u_char data[2048];
};

struct dvd_authinfo {
	unsigned char format;
	u_int8_t agid		:2;
	u_int8_t asf		:1;
	u_int8_t cpm		:1;
	u_int8_t cp_sec		:1;
	u_int8_t cgms		:2;
	u_int8_t reg_type	:2;
	u_int8_t vend_rsts	:3;
	u_int8_t user_rsts	:3;
	u_int8_t region;
	u_int8_t rpc_scheme;
	u_int32_t lba;
	u_char keychal[10];
};

#define DVD_STRUCT_PHYSICAL	0x00
#define DVD_STRUCT_COPYRIGHT	0x01
#define DVD_STRUCT_DISCKEY	0x02
#define DVD_STRUCT_BCA		0x03
#define DVD_STRUCT_MANUFACT	0x04
#define DVD_STRUCT_CMI		0x05
#define DVD_STRUCT_PROTDISCID	0x06
#define DVD_STRUCT_DISCKEYBLOCK	0x07
#define DVD_STRUCT_DDS		0x08
#define DVD_STRUCT_MEDIUM_STAT	0x09
#define DVD_STRUCT_SPARE_AREA	0x0A
#define DVD_STRUCT_RMD_LAST	0x0C
#define DVD_STRUCT_RMD_RMA	0x0D
#define DVD_STRUCT_PRERECORDED	0x0E
#define DVD_STRUCT_UNIQUEID	0x0F
#define DVD_STRUCT_DCB		0x30
#define DVD_STRUCT_LIST		0xFF

#define DVD_REPORT_AGID		0
#define DVD_REPORT_CHALLENGE	1
#define DVD_REPORT_KEY1		2
#define DVD_REPORT_TITLE_KEY	4
#define DVD_REPORT_ASF		5
#define DVD_REPORT_RPC		8
#define DVD_INVALIDATE_AGID	0x3f

#define DVD_SEND_CHALLENGE	1
#define DVD_SEND_KEY2		3
#define DVD_SEND_RPC		6

#define DVDIOCREPORTKEY		_IOWR('c', 200, struct dvd_authinfo)
#define DVDIOCSENDKEY		_IOWR('c', 201, struct dvd_authinfo)
#define DVDIOCREADSTRUCTURE	_IOWR('c', 202, struct dvd_struct)

#endif /* _SYS_DVDIO_H_ */
