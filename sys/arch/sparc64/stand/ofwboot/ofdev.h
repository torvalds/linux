/*	$OpenBSD: ofdev.h,v 1.7 2020/12/09 18:10:19 krw Exp $	*/
/*	$NetBSD: ofdev.h,v 1.1 2000/08/20 14:58:41 mrg Exp $	*/

/*
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef	_STAND_DEV_H_
#define	_STAND_DEV_H_

/* #define BOOT_DEBUG */
#ifdef BOOT_DEBUG
extern u_int32_t boot_debug;
#define DPRINTF(x...)		do { if (boot_debug) printf(x); } while(0)
#define DNPRINTF(n,x...)	do { if (boot_debug & n) printf(x); } while(0)
#define BOOT_D_OFDEV		0x0001
#define BOOT_D_OFNET		0x0002
#else
#define DPRINTF(x...)
#define DNPRINTF(n,x...)
#endif

struct of_dev {
	int handle;
	int type;
	u_long partoff;
	int bsize;
};

/* Known types: */
#define	OFDEV_NET	1
#define	OFDEV_DISK	2
#define	OFDEV_SOFTRAID	3

#define	DEFAULT_KERNEL	"/bsd"

extern char opened_name[];
extern int floppyboot;

int load_disklabel(struct of_dev *, struct disklabel *);
int strategy(void *, int, daddr_t, size_t, void *, size_t *);

int net_open(struct of_dev *);
void net_close(struct of_dev *);

#endif
