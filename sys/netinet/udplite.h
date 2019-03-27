/*-
 * Copyright (c) 2014, Kevin Lo
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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

#ifndef _NETINET_UDPLITE_H_
#define	_NETINET_UDPLITE_H_

/*
 * UDP-Lite protocol header.
 * Per RFC 3828, July, 2004.
 */
struct udplitehdr {
	u_short	udplite_sport;		/* UDO-Lite source port */
	u_short	udplite_dport;		/* UDP-Lite destination port */
	u_short	udplite_coverage;	/* UDP-Lite checksum coverage */
	u_short	udplite_checksum;	/* UDP-Lite checksum */
};

/* 
 * User-settable options (used with setsockopt).
 */
#define	UDPLITE_SEND_CSCOV	2	/* Sender checksum coverage. */
#define	UDPLITE_RECV_CSCOV	4	/* Receiver checksum coverage. */

#endif	/* !_NETINET_UDPLITE_H_ */
