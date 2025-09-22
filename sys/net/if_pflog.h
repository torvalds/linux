/* $OpenBSD: if_pflog.h,v 1.29 2021/01/13 09:13:30 mvs Exp $ */
/*
 * Copyright 2001 Niels Provos <provos@citi.umich.edu>
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

#ifndef _NET_IF_PFLOG_H_
#define _NET_IF_PFLOG_H_

#include <sys/queue.h>
#include <net/pfvar.h>

#define PFLOG_RULESET_NAME_SIZE	16

struct pfloghdr {
	u_int8_t	length;
	sa_family_t	af;
	u_int8_t	action;
	u_int8_t	reason;
	char		ifname[IFNAMSIZ];
	char		ruleset[PFLOG_RULESET_NAME_SIZE];
	u_int32_t	rulenr;
	u_int32_t	subrulenr;
	uid_t		uid;
	pid_t		pid;
	uid_t		rule_uid;
	pid_t		rule_pid;
	u_int8_t	dir;
	u_int8_t	rewritten;
	sa_family_t	naf;
	u_int8_t	pad[1];
	struct pf_addr	saddr;
	struct pf_addr	daddr;
	u_int16_t	sport;
	u_int16_t	dport;
};

#define PFLOG_HDRLEN		sizeof(struct pfloghdr)
/* used to be minus pad, also used as a signature */
#define PFLOG_REAL_HDRLEN	PFLOG_HDRLEN
#define PFLOG_OLD_HDRLEN	offsetof(struct pfloghdr, pad)

#ifdef _KERNEL

struct pflog_softc {
	LIST_ENTRY(pflog_softc)		sc_entry;
	struct ifnet			sc_if;		/* the interface */
	int				sc_unit;
};

#endif /* _KERNEL */
#endif /* _NET_IF_PFLOG_H_ */
