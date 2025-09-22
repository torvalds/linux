/*	$OpenBSD: boot_flag.h,v 1.8 2023/11/09 14:26:34 kn Exp $	*/
/*	$NetBSD: boot_flag.h,v 1.3 2001/07/01 02:56:21 gmcgarry Exp $	*/

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MACHINE_BOOT_FLAG_H_
#define _MACHINE_BOOT_FLAG_H_

/* softraid boot information */
#define BOOTSR_UUID_MAX 16
#define BOOTSR_CRYPTO_MAXKEYBYTES 32

/* MD boot data in .openbsd.bootdata ELF segment */
struct openbsd_bootdata {
	u_int64_t 	version;
	u_int64_t 	len;	/* of structure */

	u_int8_t	sr_uuid[BOOTSR_UUID_MAX];
	u_int8_t	sr_maskkey[BOOTSR_CRYPTO_MAXKEYBYTES];
	u_int32_t	boothowto;
} __packed;

#define BOOTDATA_VERSION	1
#define BOOTDATA_LEN_BOOTHOWTO	68

#endif /* _MACHINE_BOOT_FLAG_H_ */
