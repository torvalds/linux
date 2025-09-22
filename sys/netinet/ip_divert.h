/*      $OpenBSD: ip_divert.h,v 1.30 2025/06/23 12:05:46 bluhm Exp $ */

/*
 * Copyright (c) 2009 Michele Marchetto <michele@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _IP_DIVERT_H_
#define _IP_DIVERT_H_

struct divstat {
	u_long	divs_ipackets;	/* total input packets */
	u_long	divs_noport;	/* no socket on port */
	u_long	divs_fullsock;	/* not delivered, input socket full */
	u_long	divs_opackets;	/* total output packets */
	u_long	divs_errors;	/* generic errors */
};

/*
 * Names for divert sysctl objects
 */
#define	DIVERTCTL_RECVSPACE	1	/* receive buffer space */
#define	DIVERTCTL_SENDSPACE	2	/* send buffer space */
#define	DIVERTCTL_STATS		3	/* divert statistics */
#define	DIVERTCTL_MAXID		4

#define	DIVERTCTL_NAMES { \
	{ 0, 0 }, \
	{ "recvspace",	CTLTYPE_INT }, \
	{ "sendspace",	CTLTYPE_INT }, \
	{ "stats",	CTLTYPE_STRUCT } \
}

#ifdef _KERNEL

#include <sys/percpu.h>

#define DIVERT_SENDSPACE	(65536 + 100)
#define DIVERT_RECVSPACE	(65536 + 100)
#define DIVERT_HASHSIZE		128

enum divstat_counters {
	divs_ipackets,
	divs_noport,
	divs_fullsock,
	divs_opackets,
	divs_errors,
	divs_ncounters,
};

extern struct cpumem *divcounters;

static inline void
divstat_inc(enum divstat_counters c)
{
	counters_inc(divcounters, c);
}

extern u_int divert_sendspace;
extern u_int divert_recvspace;
extern struct inpcbtable divbtable, divb6table;
extern const struct pr_usrreqs divert_usrreqs, divert6_usrreqs;

void	 divert_init(void);
void	 divert_packet(struct mbuf *, int, u_int16_t);
int	 divert_sysctl(int *, u_int, void *, size_t *, void *, size_t);
int	 divert_sysctl_divstat(void *, size_t *, void *);
int	 divert_attach(struct socket *, int, int);
int	 divert_detach(struct socket *);
int	 divert_bind(struct socket *, struct mbuf *, struct proc *);
int	 divert_shutdown(struct socket *);
int	 divert_send(struct socket *, struct mbuf *, struct mbuf *,
	     struct mbuf *);

void	 divert6_init(void);
void	 divert6_packet(struct mbuf *, int, u_int16_t);
int	 divert6_attach(struct socket *, int, int);
int	 divert6_send(struct socket *, struct mbuf *, struct mbuf *,
	     struct mbuf *);

#endif /* _KERNEL */
#endif /* _IP_DIVERT_H_ */
