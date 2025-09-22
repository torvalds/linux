/*	$OpenBSD: bppioctl.h,v 1.2 2008/11/29 01:55:06 ray Exp $	*/

/*-
 * Copyright (c) 1998 Iain Hibbert
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
 */

#ifndef _IOCTL_
#include <sys/ioctl.h>
#endif

struct bpp_param {
	int	bp_burst;	/* chars to send/recv in one call */
	int	bp_timeout;	/* timeout: -1 blocking, 0 non blocking >0 ms */
	int	bp_delay;	/* delay between polls (ms) */
};

#define BPP_BLOCK	-1
#define BPP_NOBLOCK	0

/* defaults */
#define BPP_BURST	1024
#define BPP_TIMEOUT	BPP_BLOCK
#define BPP_DELAY	10

/* limits */
#define BPP_BURST_MIN	1
#define BPP_BURST_MAX	1024
#define BPP_DELAY_MIN	0
#define BPP_DELAY_MAX	30000

/* status bits */
#define BPP_BUSY	(1<<0)
#define BPP_PAPER	(1<<1)

/* ioctl commands */
#define BPPIOCSPARAM	_IOW('P', 0x1, struct bpp_param)
#define BPPIOCGPARAM	_IOR('P', 0x2, struct bpp_param)
#define BPPIOCGSTAT	_IOR('P', 0x4, int)
