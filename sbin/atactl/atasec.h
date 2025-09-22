/*	$OpenBSD: atasec.h,v 1.1 2002/07/06 14:46:57 gluk Exp $	*/

/*
 * Copyright (c) 2002 Alexander Yurchenko <grange@rt.mipt.ru>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* ATA Security Mode commands */
#define ATA_SEC_SET_PASSWORD		0xf1
#define ATA_SEC_UNLOCK			0xf2
#define ATA_SEC_ERASE_PREPARE		0xf3
#define	ATA_SEC_ERASE_UNIT		0xf4
#define ATA_SEC_FREEZE_LOCK		0xf5
#define ATA_SEC_DISABLE_PASSWORD	0xf6

/* security password sector */
struct sec_password {
	u_int16_t ctrl;			/* Control word */
#define SEC_PASSWORD_USER	0x0000
#define SEC_PASSWORD_MASTER	0x0001
#define SEC_ERASE_NORMAL	0x0000
#define SEC_ERASE_ENHANCED	0x0002
#define SEC_LEVEL_HIGH		0x0000
#define SEC_LEVEL_MAX		0x0100
	u_int8_t  password[32];		/* Password */
	u_int16_t revision;		/* Master password revision code */
	u_int16_t res[238];		/* Reserved */
};
