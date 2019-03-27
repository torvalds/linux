/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Matthew N. Dodd <winter@freebsd.org>
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

#ifndef _MACHINE_SMAPI_H_
#define	_MACHINE_SMAPI_H_

#ifndef _KERNEL
#include <sys/types.h>
#endif
#include <sys/ioccom.h>

struct smapi_bios_header {
	u_int8_t	signature[4];	/* '$SMB' */
	u_int8_t	version_major;
	u_int8_t	version_minor;
	u_int8_t	length;
	u_int8_t	checksum;
	u_int16_t	information;
#define	SMAPI_REAL_VM86		0x0001
#define	SMAPI_PROT_16BIT	0x0002
#define	SMAPI_PROT_32BIT	0x0004
	u_int16_t	reserved1;

	u_int16_t	real16_offset;
	u_int16_t	real16_segment;

	u_int16_t	reserved2;

	u_int16_t	prot16_offset;
	u_int32_t	prot16_segment;

	u_int32_t	prot32_offset;
	u_int32_t	prot32_segment;
	
} __packed;

struct smapi_bios_parameter {
	union {
		struct {
			u_int8_t func;
			u_int8_t sub_func;
		} in;
		struct {
			u_int8_t rc;
			u_int8_t sub_rc;
		} out;
	} type;

	u_int16_t	param1;
	u_int16_t	param2;
	u_int16_t	param3;

	u_int32_t	param4;
	u_int32_t	param5;

} __packed;

#define	cmd_func	type.in.func
#define	cmd_sub_func	type.in.sub_func
#define	rsp_rc		type.out.rc
#define	rsp_sub_rc	type.out.sub_rc

#define	SMAPIOGHEADER		_IOR('$', 0, struct smapi_bios_header)
#define SMAPIOCGFUNCTION	_IOWR('$', 1, struct smapi_bios_parameter)

#endif	/* _MACHINE_SMAPI_H_ */
