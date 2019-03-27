/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Adrian Chadd, Xenion Lty Ltd
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
 *
 * $FreeBSD$
 */
#ifndef	__IEEE80211_ALQ_H__
#define	__IEEE80211_ALQ_H__

#define	IEEE80211_ALQ_MAX_PAYLOAD	1024

/*
 * timestamp
 * wlan interface
 * operation
 * sub-operation
 * rest of structure - operation specific
 */

#define	IEEE80211_ALQ_SRC_NET80211	0x0001
/* Drivers define their own numbers above 0xff */

struct ieee80211_alq_rec {
	uint64_t	r_timestamp;	/* XXX may wrap! */
	uint32_t	r_threadid;	/* current thread id */
	uint16_t	r_wlan;		/* wlan interface number */
	uint16_t	r_src;		/* source - driver, net80211 */
	uint32_t	r_flags;	/* flags */
	uint32_t	r_op;		/* top-level operation id */
	uint32_t	r_len;		/* length of hdr + payload */
	/* Operation payload follows here */
};

/* General logging function */
extern	int ieee80211_alq_log(struct ieee80211com *ic,
	    struct ieee80211vap *vap, uint32_t op, uint32_t flags,
	    uint16_t srcid, const uint8_t *src, size_t len);

#endif	/* __IEEE80211_ALQ_H__ */
