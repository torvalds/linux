/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$KAME: pim6.h,v 1.3 2000/03/25 07:23:58 sumikawa Exp $
 * $FreeBSD$
 */
/*
 * Protocol Independent Multicast (PIM) definitions
 *
 * Written by Ahmed Helmy, SGI, July 1996
 *
 * MULTICAST
 */

/*
 * PIM packet header
 */
#define PIM_VERSION	2
struct pim {
#if defined(BYTE_ORDER) && (BYTE_ORDER == LITTLE_ENDIAN)
	u_char	pim_type:4, /* the PIM message type, currently they are:
			     * Hello, Register, Register-Stop, Join/Prune,
			     * Bootstrap, Assert, Graft (PIM-DM only),
			     * Graft-Ack (PIM-DM only), C-RP-Adv
			     */
		pim_ver:4;  /* PIM version number; 2 for PIMv2 */
#else
	u_char	pim_ver:4,	/* PIM version */
		pim_type:4;	/* PIM type    */
#endif
	u_char  pim_rsv;	/* Reserved */
	u_short	pim_cksum;	/* IP style check sum */
};

#define PIM_MINLEN	8		/* The header min. length is 8    */
#define PIM6_REG_MINLEN	(PIM_MINLEN+40)	/* Register message + inner IP6 header */

/*
 * Message types
 */
#define PIM_REGISTER	1	/* PIM Register type is 1 */

/* second bit in reg_head is the null bit */
#define PIM_NULL_REGISTER 0x40000000
