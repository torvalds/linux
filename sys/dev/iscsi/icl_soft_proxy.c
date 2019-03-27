/*-
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
/*-
 * Copyright (c) 1982, 1986, 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * sendfile(2) and related extensions:
 * Copyright (c) 1998, David Greenman. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *	@(#)uipc_syscalls.c	8.4 (Berkeley) 2/21/94
 */

/*
 * iSCSI Common Layer, kernel proxy part.
 */

#ifdef ICL_KERNEL_PROXY

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/condvar.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sx.h>
#include <sys/systm.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <dev/iscsi/icl.h>

struct icl_listen_sock {
	TAILQ_ENTRY(icl_listen_sock)	ils_next;
	struct icl_listen		*ils_listen;
	struct socket			*ils_socket;
	bool				ils_running;
	int				ils_id;
};

struct icl_listen	{
	TAILQ_HEAD(, icl_listen_sock)	il_sockets;
	struct sx			il_lock;
	void				(*il_accept)(struct socket *,
					    struct sockaddr *, int);
};

static MALLOC_DEFINE(M_ICL_PROXY, "ICL_PROXY", "iSCSI common layer proxy");

int
icl_soft_proxy_connect(struct icl_conn *ic, int domain, int socktype,
    int protocol, struct sockaddr *from_sa, struct sockaddr *to_sa)
{
	struct socket *so;
	int error;
	int interrupted = 0;

	error = socreate(domain, &so, socktype, protocol,
	    curthread->td_ucred, curthread);
	if (error != 0)
		return (error);

	if (from_sa != NULL) {
		error = sobind(so, from_sa, curthread);
		if (error != 0) {
			soclose(so);
			return (error);
		}
	}

	error = soconnect(so, to_sa, curthread);
	if (error != 0) {
		soclose(so);
		return (error);
	}

	SOCK_LOCK(so);
	while ((so->so_state & SS_ISCONNECTING) && so->so_error == 0) {
		error = msleep(&so->so_timeo, SOCK_MTX(so), PSOCK | PCATCH,
		    "icl_connect", 0);
		if (error) {
			if (error == EINTR || error == ERESTART)
				interrupted = 1;
			break;
		}
	}
	if (error == 0) {
		error = so->so_error;
		so->so_error = 0;
	}
	SOCK_UNLOCK(so);

	if (error != 0) {
		soclose(so);
		return (error);
	}

	error = icl_soft_handoff_sock(ic, so);
	if (error != 0)
		soclose(so);

	return (error);
}

struct icl_listen *
icl_listen_new(void (*accept_cb)(struct socket *, struct sockaddr *, int))
{
	struct icl_listen *il;

	il = malloc(sizeof(*il), M_ICL_PROXY, M_ZERO | M_WAITOK);
	TAILQ_INIT(&il->il_sockets);
	sx_init(&il->il_lock, "icl_listen");
	il->il_accept = accept_cb;

	return (il);
}

void
icl_listen_free(struct icl_listen *il)
{
	struct icl_listen_sock *ils;

	sx_xlock(&il->il_lock);
	while (!TAILQ_EMPTY(&il->il_sockets)) {
		ils = TAILQ_FIRST(&il->il_sockets);
		while (ils->ils_running) {
			ICL_DEBUG("waiting for accept thread to terminate");
			sx_xunlock(&il->il_lock);
			SOLISTEN_LOCK(ils->ils_socket);
			ils->ils_socket->so_error = ENOTCONN;
			SOLISTEN_UNLOCK(ils->ils_socket);
			wakeup(&ils->ils_socket->so_timeo);
			pause("icl_unlisten", 1 * hz);
			sx_xlock(&il->il_lock);
		}
	
		TAILQ_REMOVE(&il->il_sockets, ils, ils_next);
		soclose(ils->ils_socket);
		free(ils, M_ICL_PROXY);
	}
	sx_xunlock(&il->il_lock);

	free(il, M_ICL_PROXY);
}

/*
 * XXX: Doing accept in a separate thread in each socket might not be the
 * best way to do stuff, but it's pretty clean and debuggable - and you
 * probably won't have hundreds of listening sockets anyway.
 */
static void
icl_accept_thread(void *arg)
{
	struct icl_listen_sock *ils;
	struct socket *head, *so;
	struct sockaddr *sa;
	int error;

	ils = arg;
	head = ils->ils_socket;

	ils->ils_running = true;

	for (;;) {
		SOLISTEN_LOCK(head);
		error = solisten_dequeue(head, &so, 0);
		if (error == ENOTCONN) {
			/*
			 * XXXGL: ENOTCONN is our mark from icl_listen_free().
			 * Neither socket code, nor msleep(9) may return it.
			 */
			ICL_DEBUG("terminating");
			ils->ils_running = false;
			kthread_exit();
			return;
		}
		if (error) {
			ICL_WARN("solisten_dequeue error %d", error);
			continue;
		}

		sa = NULL;
		error = soaccept(so, &sa);
		if (error != 0) {
			ICL_WARN("soaccept error %d", error);
			if (sa != NULL)
				free(sa, M_SONAME);
			soclose(so);
			continue;
		}

		(ils->ils_listen->il_accept)(so, sa, ils->ils_id);
	}
}

static int
icl_listen_add_tcp(struct icl_listen *il, int domain, int socktype,
    int protocol, struct sockaddr *sa, int portal_id)
{
	struct icl_listen_sock *ils;
	struct socket *so;
	struct sockopt sopt;
	int error, one = 1;

	error = socreate(domain, &so, socktype, protocol,
	    curthread->td_ucred, curthread);
	if (error != 0) {
		ICL_WARN("socreate failed with error %d", error);
		return (error);
	}

	sopt.sopt_dir = SOPT_SET;
	sopt.sopt_level = SOL_SOCKET;
	sopt.sopt_name = SO_REUSEADDR;
	sopt.sopt_val = &one;
	sopt.sopt_valsize = sizeof(one);
	sopt.sopt_td = NULL;
	error = sosetopt(so, &sopt);
	if (error != 0) {
		ICL_WARN("failed to set SO_REUSEADDR with error %d", error);
		soclose(so);
		return (error);
	}

	error = sobind(so, sa, curthread);
	if (error != 0) {
		ICL_WARN("sobind failed with error %d", error);
		soclose(so);
		return (error);
	}

	error = solisten(so, -1, curthread);
	if (error != 0) {
		ICL_WARN("solisten failed with error %d", error);
		soclose(so);
		return (error);
	}

	ils = malloc(sizeof(*ils), M_ICL_PROXY, M_ZERO | M_WAITOK);
	ils->ils_listen = il;
	ils->ils_socket = so;
	ils->ils_id = portal_id;

	error = kthread_add(icl_accept_thread, ils, NULL, NULL, 0, 0, "iclacc");
	if (error != 0) {
		ICL_WARN("kthread_add failed with error %d", error);
		soclose(so);
		free(ils, M_ICL_PROXY);

		return (error);
	}

	sx_xlock(&il->il_lock);
	TAILQ_INSERT_TAIL(&il->il_sockets, ils, ils_next);
	sx_xunlock(&il->il_lock);

	return (0);
}

int
icl_listen_add(struct icl_listen *il, bool rdma, int domain, int socktype,
    int protocol, struct sockaddr *sa, int portal_id)
{

	if (rdma) {
		ICL_DEBUG("RDMA not supported");
		return (EOPNOTSUPP);
	}


	return (icl_listen_add_tcp(il, domain, socktype, protocol, sa,
	    portal_id));
}

int
icl_listen_remove(struct icl_listen *il, struct sockaddr *sa)
{

	/*
	 * XXX
	 */

	return (EOPNOTSUPP);
}

#endif /* ICL_KERNEL_PROXY */
