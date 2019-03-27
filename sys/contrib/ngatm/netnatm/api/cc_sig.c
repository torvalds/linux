/*
* Copyright (c) 2004
*	Hartmut Brandt
*	All rights reserved.
*
* Author: Harti Brandt <harti@freebsd.org>
*
* Redistribution of this software and documentation and use in source and
* binary forms, with or without modification, are permitted provided that
* the following conditions are met:
*
* 1. Redistributions of source code or documentation must retain the above
*    copyright notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the distribution.
*
* THIS SOFTWARE AND DOCUMENTATION IS PROVIDED BY THE AUTHOR
* AND ITS CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
* INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
* FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
* THE AUTHOR OR ITS CONTRIBUTORS  BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* $Begemot: libunimsg/netnatm/api/cc_sig.c,v 1.1 2004/07/08 08:21:54 brandt Exp $
*
* ATM API as defined per af-saa-0108
*
* Generic signal handling
*/
#include <netnatm/unimsg.h>
#include <netnatm/msg/unistruct.h>
#include <netnatm/msg/unimsglib.h>
#include <netnatm/api/unisap.h>
#include <netnatm/sig/unidef.h>
#include <netnatm/api/atmapi.h>
#include <netnatm/api/ccatm.h>
#include <netnatm/api/ccpriv.h>

enum {
	SIG_USER,
	SIG_CONN,
};

struct ccsig {
	u_char	type;		/* type of target */
	u_char	has_msg;	/* arg1 is a message */
	void	*target;	/* target instance */
	u_int	sig;		/* signal */
	void	*arg1;		/* argument */
	u_int	arg2;		/* argument */
	TAILQ_ENTRY(ccsig) link;
};

#if defined(__GNUC__) && __GNUC__ < 3
#define	cc_sig_log(CC, FMT, ARGS...) do {				\
	if ((CC)->log & CCLOG_SIGS)					\
		(CC)->funcs->log("%s: " FMT, __FUNCTION__ , ## ARGS);	\
    } while (0)
#else
#define	cc_sig_log(CC, FMT, ...) do {					\
	if ((CC)->log & CCLOG_SIGS)					\
		(CC)->funcs->log("%s: " FMT, __func__, __VA_ARGS__);	\
    } while (0)
#endif


const char *const cc_user_sigtab[] = {
#define	DEF(N) [USER_SIG_##N] = #N,
USER_SIGS
#undef DEF
};

const char *const cc_conn_sigtab[] = {
#define	DEF(N) [CONN_SIG_##N] = #N,
CONN_SIGS
#undef DEF
};


/*
 * Allocate and populate a signal
 */
static /* __inline */ struct ccsig *
sig_alloc(struct ccdata *cc, u_int type, void *target, u_int has_msg,
    u_int sig, void *arg1, u_int arg2)
{
	struct ccsig *s;

	if ((s = TAILQ_FIRST(&cc->free_sigs)) == NULL) {
		s = CCZALLOC(sizeof(struct ccsig));
		if (s == NULL) {
			cc_log(cc, "signal %u/%u lost - ENOMEM", type, sig);
			return (NULL);
		}
	} else
		TAILQ_REMOVE(&cc->free_sigs, s, link);

	s->type = type;
	s->has_msg = has_msg;
	s->target = target;
	s->sig = sig;
	s->arg1 = arg1;
	s->arg2 = arg2;

	return (s);
}

/*
 * Queue a signal to this user
 */
int
cc_user_sig(struct ccuser *user, enum user_sig sig, void *arg1, u_int arg2)
{
	struct ccsig *s;

	s = sig_alloc(user->cc, SIG_USER, user, 0, sig, arg1, arg2);
	if (s == NULL)
		return (ENOMEM);
	TAILQ_INSERT_TAIL(&user->cc->sigs, s, link);
	cc_sig_log(user->cc, "queuing sig %s to user %p", cc_user_sigtab[sig],
	    user);
	return (0);
}

/* Queue a signal with message to this user */
int
cc_user_sig_msg(struct ccuser *user, enum user_sig sig, struct uni_msg *msg)
{
	struct ccsig *s;

	s = sig_alloc(user->cc, SIG_USER, user, msg != NULL, sig, msg, 0);
	if (s == NULL)
		return (ENOMEM);
	TAILQ_INSERT_TAIL(&user->cc->sigs, s, link);
	cc_sig_log(user->cc, "queuing sig %s to user %p", cc_user_sigtab[sig],
	    user);
	return (0);
}

/*
 * Signal to connection
 */
static int
sig_conn(struct ccconn *conn, enum conn_sig sig, u_int has_msg, void *arg)
{
	struct ccsig *s;
	const struct ccreq *r = NULL;

	s = sig_alloc(conn->cc, SIG_CONN, conn, has_msg, sig, arg, 0);
	if (s == NULL)
		return (ENOMEM);

	if (conn->port != NULL) {
		/* argh */
		TAILQ_FOREACH(r, &conn->port->cookies, link)
			if (r->conn == conn)
				break;
	}
	if (r == NULL) {
		TAILQ_INSERT_TAIL(&conn->cc->sigs, s, link);
		cc_sig_log(conn->cc, "queuing sig %s to conn %p",
		    cc_conn_sigtab[sig], conn);
	} else {
		TAILQ_INSERT_TAIL(&conn->cc->def_sigs, s, link);
		cc_sig_log(conn->cc, "queuing defered sig %s to conn %p",
		    cc_conn_sigtab[sig], conn);
	}
	return (0);
}

/*
 * Queue a signal to a connection.
 */
int
cc_conn_sig(struct ccconn *conn, enum conn_sig sig, void *arg1)
{

	return (sig_conn(conn, sig, 0, arg1));
}

/*
 * signal with message to connection
 */
int
cc_conn_sig_msg(struct ccconn *conn, enum conn_sig sig, struct uni_msg *msg)
{

	return (sig_conn(conn, sig, (msg != NULL), msg));
}
int
cc_conn_sig_msg_nodef(struct ccconn *conn, enum conn_sig sig,
    struct uni_msg *msg)
{
	struct ccsig *s;

	s = sig_alloc(conn->cc, SIG_CONN, conn, (msg != NULL), sig, msg, 0);
	if (s == NULL)
		return (ENOMEM);

	TAILQ_INSERT_TAIL(&conn->cc->sigs, s, link);
	cc_sig_log(conn->cc, "queuing sig %s to conn %p",
	    cc_conn_sigtab[sig], conn);

	return (0);
}

/*
 * Queue a response signal to a connection.
 */
int
cc_conn_resp(struct ccconn *conn, enum conn_sig sig, u_int cookie __unused,
    u_int reason, u_int state)
{
	struct ccsig *s, *s1, *s2;

	s = sig_alloc(conn->cc, SIG_CONN, conn, 0, sig, NULL,
	     ((reason & 0xffff) << 16) | (state & 0xffff));
	if (s == NULL)
		return (ENOMEM);

	TAILQ_INSERT_TAIL(&conn->cc->sigs, s, link);

	cc_sig_log(conn->cc, "queuing response %s to conn %p",
	    cc_conn_sigtab[sig], conn);

	s1 = TAILQ_FIRST(&conn->cc->def_sigs);
	while (s1 != NULL) {
		s2 = TAILQ_NEXT(s1, link);
		if (s1->type == SIG_CONN && s1->target == conn) {
			TAILQ_REMOVE(&conn->cc->def_sigs, s1, link);
			TAILQ_INSERT_AFTER(&conn->cc->sigs, s, s1, link);
			cc_sig_log(conn->cc, "undefering sig %s to conn %p",
			    cc_conn_sigtab[s1->sig], conn);
			s = s1;
		}
		s1 = s2;
	}

	return (0);
}

/*
 * Flush all signals to a given target from both queues
 */
static /* __inline */ void
sig_flush(struct ccdata *cc, u_int type, void *target)
{
	struct ccsig *s, *s1;

	s = TAILQ_FIRST(&cc->sigs);
	while (s != NULL) {
		s1 = TAILQ_NEXT(s, link);
		if (s->type == type && s->target == target) {
			if (s->has_msg)
				uni_msg_destroy((struct uni_msg *)s->arg1);
			TAILQ_REMOVE(&cc->sigs, s, link);
			TAILQ_INSERT_HEAD(&cc->free_sigs, s, link);
		}
		s = s1;
	}

	s = TAILQ_FIRST(&cc->def_sigs);
	while (s != NULL) {
		s1 = TAILQ_NEXT(s, link);
		if (s->type == type && s->target == target) {
			if (s->has_msg)
				uni_msg_destroy((struct uni_msg *)s->arg1);
			TAILQ_REMOVE(&cc->def_sigs, s, link);
			TAILQ_INSERT_HEAD(&cc->free_sigs, s, link);
		}
		s = s1;
	}
}

/*
 * Flush all signals to this user
 */
void
cc_user_sig_flush(struct ccuser *user)
{

	cc_sig_log(user->cc, "flushing signals to user %p", user);
	sig_flush(user->cc, SIG_USER, user);
}

/*
 * Flush all signals to this connection
 */
void
cc_conn_sig_flush(struct ccconn *conn)
{

	cc_sig_log(conn->cc, "flushing signals to conn %p", conn);
	sig_flush(conn->cc, SIG_CONN, conn);
}

/*
 * Do the work
 */
void
cc_work(struct ccdata *cc)
{
	struct ccsig *s;

	cc_sig_log(cc, "start %s", "work");
	while ((s = TAILQ_FIRST(&cc->sigs)) != NULL) {
		TAILQ_REMOVE(&cc->sigs, s, link);
		if (s->type == SIG_USER)
			cc_user_sig_handle(s->target, s->sig, s->arg1, s->arg2);
		else {
			cc_conn_sig_handle(s->target, s->sig, s->arg1, s->arg2);
			if (s->has_msg)
				uni_msg_destroy(s->arg1);
		}
		TAILQ_INSERT_HEAD(&cc->free_sigs, s, link);
	}
	cc_sig_log(cc, "end %s", "work");
}

/*
 * flush all signals
 */
void
cc_sig_flush_all(struct ccdata *cc)
{
	struct ccsig *s;

	while ((s = TAILQ_FIRST(&cc->sigs)) != NULL) {
		if (s->has_msg)
			uni_msg_destroy((struct uni_msg *)s->arg1);
		TAILQ_REMOVE(&cc->sigs, s, link);
		CCFREE(s);
	}
	while ((s = TAILQ_FIRST(&cc->def_sigs)) != NULL) {
		if (s->has_msg)
			uni_msg_destroy((struct uni_msg *)s->arg1);
		TAILQ_REMOVE(&cc->def_sigs, s, link);
		CCFREE(s);
	}
	while ((s = TAILQ_FIRST(&cc->free_sigs)) != NULL) {
		TAILQ_REMOVE(&cc->free_sigs, s, link);
		CCFREE(s);
	}
}
