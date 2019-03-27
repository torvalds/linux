/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Adam Glass.
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
 *
 *	from: NetBSD: idprom.h,v 1.3 2008/04/28 20:23:58 martin Exp
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_IDPROM_H_
#define _MACHINE_IDPROM_H_

/*
 * ID prom format.  The ``host id'' is set up by taking the machine
 * ID as the top byte and the hostid field as the remaining three.
 * The id_xxx0 field appears to contain some other number.  The id_xxx1
 * contains a bunch of 00's and a5's on my machines, suggesting it is
 * not actually used.  The checksum seems to include them, however.
 */
struct idprom {
	u_char	id_format;		/* format identifier (= 1) */
	u_char	id_machine;		/* machine type (see param.h) */
	u_char	id_ether[6];		/* ethernet address */
	int	id_date;		/* date of manufacture */
	u_char	id_hostid[3];		/* ``host id'' bytes */
	u_char	id_checksum;		/* xor of everything else */
	/* Note: The rest is excluded from the checksum! */
	char	id_undef[16];		/* undefined */
};

#define	ID_SUN4_100	0x22	/* Sun 4/100 */
#define	ID_SUN4_200	0x21	/* Sun 4/200 */
#define	ID_SUN4_300	0x23	/* Sun 4/300 */
#define	ID_SUN4_400	0x24	/* Sun 4/400 */

#define IDPROM_VERSION 1

#endif /* !_MACHINE_IDPROM_H_ */
