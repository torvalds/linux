/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
#ifndef	_LINUX_NET_H_
#define	_LINUX_NET_H_

#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/errno.h>

static inline int
sock_create_kern(int family, int type, int proto, struct socket **res)
{
	return -socreate(family, res, type, proto, curthread->td_ucred,
	    curthread);
}

static inline int
sock_getname(struct socket *so, struct sockaddr *addr, int *sockaddr_len,
    int peer)
{
	struct sockaddr *nam;
	int error;

	nam = NULL;
	if (peer) {
		if ((so->so_state & (SS_ISCONNECTED|SS_ISCONFIRMING)) == 0)
			return (-ENOTCONN);

		error = (*so->so_proto->pr_usrreqs->pru_peeraddr)(so, &nam);
	} else
		error = (*so->so_proto->pr_usrreqs->pru_sockaddr)(so, &nam);
	if (error)
		return (-error);
	*addr = *nam;
	*sockaddr_len = addr->sa_len;

	free(nam, M_SONAME);
	return (0);
}

static inline void
sock_release(struct socket *so)
{
	soclose(so);
}

#endif	/* _LINUX_NET_H_ */
