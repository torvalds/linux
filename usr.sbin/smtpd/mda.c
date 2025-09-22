/*	$OpenBSD: mda.c,v 1.147 2024/01/20 09:01:03 claudio Exp $	*/

/*
 * Copyright (c) 2008 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2008 Pierre-Yves Ritschard <pyr@openbsd.org>
 * Copyright (c) 2009 Jacek Masiulaniec <jacekm@dobremiasto.net>
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
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

#include <ctype.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>
#include <vis.h>

#include "smtpd.h"
#include "log.h"

#define MDA_HIWAT		65536

struct mda_envelope {
	TAILQ_ENTRY(mda_envelope)	 entry;
	uint64_t			 session_id;
	uint64_t			 id;
	time_t				 creation;
	char				*sender;
	char				*rcpt;
	char				*dest;
	char				*user;
	char				*dispatcher;
	char				*mda_subaddress;
	char				*mda_exec;
};

#define USER_WAITINFO	0x01
#define USER_RUNNABLE	0x02
#define USER_ONHOLD	0x04
#define USER_HOLDQ	0x08

struct mda_user {
	uint64_t			id;
	TAILQ_ENTRY(mda_user)		entry;
	TAILQ_ENTRY(mda_user)		entry_runnable;
	char				name[LOGIN_NAME_MAX];
	char				usertable[PATH_MAX];
	size_t				evpcount;
	TAILQ_HEAD(, mda_envelope)	envelopes;
	int				flags;
	size_t				running;
	struct userinfo			userinfo;
};

struct mda_session {
	uint64_t		 id;
	struct mda_user		*user;
	struct mda_envelope	*evp;
	struct io		*io;
	FILE			*datafp;
};

static void mda_io(struct io *, int, void *);
static int mda_check_loop(FILE *, struct mda_envelope *);
static int mda_getlastline(int, char *, size_t);
static void mda_done(struct mda_session *);
static void mda_fail(struct mda_user *, int, const char *,
    enum enhanced_status_code);
static void mda_drain(void);
static void mda_log(const struct mda_envelope *, const char *, const char *);
static void mda_queue_ok(uint64_t);
static void mda_queue_tempfail(uint64_t, const char *,
    enum enhanced_status_code);
static void mda_queue_permfail(uint64_t, const char *, enum enhanced_status_code);
static void mda_queue_loop(uint64_t);
static struct mda_user *mda_user(const struct envelope *);
static void mda_user_free(struct mda_user *);
static const char *mda_user_to_text(const struct mda_user *);
static struct mda_envelope *mda_envelope(uint64_t, const struct envelope *);
static void mda_envelope_free(struct mda_envelope *);
static struct mda_session * mda_session(struct mda_user *);
static const char *mda_sysexit_to_str(int);

static struct tree	sessions;
static struct tree	users;

static TAILQ_HEAD(, mda_user)	runnable;

void
mda_imsg(struct mproc *p, struct imsg *imsg)
{
	struct mda_session	*s;
	struct mda_user		*u;
	struct mda_envelope	*e;
	struct envelope		 evp;
	struct deliver		 deliver;
	struct msg		 m;
	const void		*data;
	const char		*error, *parent_error, *syserror;
	uint64_t		 reqid;
	size_t			 sz;
	char			 out[256], buf[LINE_MAX];
	int			 n, fd;
	enum lka_resp_status	status;
	enum mda_resp_status	mda_status;
	int			mda_sysexit;

	switch (imsg->hdr.type) {
	case IMSG_MDA_LOOKUP_USERINFO:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_int(&m, (int *)&status);
		if (status == LKA_OK)
			m_get_data(&m, &data, &sz);
		m_end(&m);

		u = tree_xget(&users, reqid);

		if (status == LKA_TEMPFAIL)
			mda_fail(u, 0,
			    "Temporary failure in user lookup",
			    ESC_OTHER_ADDRESS_STATUS);
		else if (status == LKA_PERMFAIL)
			mda_fail(u, 1,
			    "Permanent failure in user lookup",
			    ESC_DESTINATION_MAILBOX_HAS_MOVED);
		else {
			if (sz != sizeof(u->userinfo))
				fatalx("mda: userinfo size mismatch");
			memmove(&u->userinfo, data, sz);
			u->flags &= ~USER_WAITINFO;
			u->flags |= USER_RUNNABLE;
			TAILQ_INSERT_TAIL(&runnable, u, entry_runnable);
			mda_drain();
		}
		return;

	case IMSG_QUEUE_DELIVER:
		m_msg(&m, imsg);
		m_get_envelope(&m, &evp);
		m_end(&m);

		u = mda_user(&evp);

		if (u->evpcount >= env->sc_mda_task_hiwat) {
			if (!(u->flags & USER_ONHOLD)) {
				log_debug("debug: mda: hiwat reached for "
				    "user \"%s\": holding envelopes",
				    mda_user_to_text(u));
				u->flags |= USER_ONHOLD;
			}
		}

		if (u->flags & USER_ONHOLD) {
			u->flags |= USER_HOLDQ;
			m_create(p_queue, IMSG_MDA_DELIVERY_HOLD,
			    0, 0, -1);
			m_add_evpid(p_queue, evp.id);
			m_add_id(p_queue, u->id);
			m_close(p_queue);
			return;
		}

		e = mda_envelope(u->id, &evp);
		TAILQ_INSERT_TAIL(&u->envelopes, e, entry);
		u->evpcount += 1;
		stat_increment("mda.pending", 1);

		if (!(u->flags & USER_RUNNABLE) &&
		    !(u->flags & USER_WAITINFO)) {
			u->flags |= USER_RUNNABLE;
			TAILQ_INSERT_TAIL(&runnable, u, entry_runnable);
		}

		mda_drain();
		return;

	case IMSG_MDA_OPEN_MESSAGE:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_end(&m);

		s = tree_xget(&sessions, reqid);
		e = s->evp;

		fd = imsg_get_fd(imsg);
		if (fd == -1) {
			log_debug("debug: mda: cannot get message fd");
			mda_queue_tempfail(e->id,
			    "Cannot get message fd",
			    ESC_OTHER_MAIL_SYSTEM_STATUS);
			mda_log(e, "TempFail", "Cannot get message fd");
			mda_done(s);
			return;
		}

		log_debug("debug: mda: got message fd %d "
		    "for session %016"PRIx64 " evpid %016"PRIx64,
		    fd, s->id, e->id);

		if ((s->datafp = fdopen(fd, "r")) == NULL) {
			log_warn("warn: mda: fdopen");
			close(fd);
			mda_queue_tempfail(e->id, "fdopen failed",
			    ESC_OTHER_MAIL_SYSTEM_STATUS);
			mda_log(e, "TempFail", "fdopen failed");
			mda_done(s);
			return;
		}

		/* check delivery loop */
		if (mda_check_loop(s->datafp, e)) {
			log_debug("debug: mda: loop detected");
			mda_queue_loop(e->id);
			mda_log(e, "PermFail", "Loop detected");
			mda_done(s);
			return;
		}

		/* start queueing delivery headers */
		if (e->sender[0])
			/*
			 * XXX: remove existing Return-Path,
			 * if any
			 */
			n = io_printf(s->io,
			    "Return-Path: <%s>\n"
			    "Delivered-To: %s\n",
			    e->sender,
			    e->rcpt ? e->rcpt : e->dest);
		else
			n = io_printf(s->io,
			    "Delivered-To: %s\n",
			    e->rcpt ? e->rcpt : e->dest);
		if (n == -1) {
			log_warn("warn: mda: "
			    "fail to write delivery info");
			mda_queue_tempfail(e->id, "Out of memory",
			    ESC_OTHER_MAIL_SYSTEM_STATUS);
			mda_log(e, "TempFail", "Out of memory");
			mda_done(s);
			return;
		}

		/* request parent to fork a helper process */
		memset(&deliver, 0, sizeof deliver);
		(void)text_to_mailaddr(&deliver.sender, s->evp->sender);
		(void)text_to_mailaddr(&deliver.rcpt, s->evp->rcpt);
		(void)text_to_mailaddr(&deliver.dest, s->evp->dest);
		if (s->evp->mda_exec)
			(void)strlcpy(deliver.mda_exec, s->evp->mda_exec, sizeof deliver.mda_exec);
		if (s->evp->mda_subaddress)
			(void)strlcpy(deliver.mda_subaddress, s->evp->mda_subaddress, sizeof deliver.mda_subaddress);
		(void)strlcpy(deliver.dispatcher, s->evp->dispatcher, sizeof deliver.dispatcher);
		deliver.userinfo = s->user->userinfo;

		log_debug("debug: mda: querying mda fd "
		    "for session %016"PRIx64 " evpid %016"PRIx64,
		    s->id, s->evp->id);

		m_create(p_parent, IMSG_MDA_FORK, 0, 0, -1);
		m_add_id(p_parent, reqid);
		m_add_data(p_parent, &deliver, sizeof(deliver));
		m_close(p_parent);
		return;

	case IMSG_MDA_FORK:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_end(&m);

		s = tree_xget(&sessions, reqid);
		e = s->evp;
		fd = imsg_get_fd(imsg);
		if (fd == -1) {
			log_warn("warn: mda: fail to retrieve mda fd");
			mda_queue_tempfail(e->id, "Cannot get mda fd",
			    ESC_OTHER_MAIL_SYSTEM_STATUS);
			mda_log(e, "TempFail", "Cannot get mda fd");
			mda_done(s);
			return;
		}

		log_debug("debug: mda: got mda fd %d "
		    "for session %016"PRIx64 " evpid %016"PRIx64,
		    fd, s->id, s->evp->id);

		io_set_nonblocking(fd);
		io_set_fd(s->io, fd);
		io_set_write(s->io);
		return;

	case IMSG_MDA_DONE:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_int(&m, (int *)&mda_status);
		m_get_int(&m, (int *)&mda_sysexit);
		m_get_string(&m, &parent_error);
		m_end(&m);

		s = tree_xget(&sessions, reqid);
		e = s->evp;
		/*
		 * Grab last line of mda stdout/stderr if available.
		 */
		out[0] = '\0';
		fd = imsg_get_fd(imsg);
		if (fd != -1)
			mda_getlastline(fd, out, sizeof(out));

		/*
		 * Choose between parent's description of error and
		 * child's output, the latter having preference over
		 * the former.
		 */
		error = NULL;
		if (mda_status == MDA_OK) {
			if (s->datafp || (s->io && io_queued(s->io))) {
				error = "mda exited prematurely";
				mda_status = MDA_TEMPFAIL;
			}
		} else
			error = out[0] ? out : parent_error;

		syserror = NULL;
		if (mda_sysexit)
			syserror = mda_sysexit_to_str(mda_sysexit);
		
		/* update queue entry */
		switch (mda_status) {
		case MDA_TEMPFAIL:
			mda_queue_tempfail(e->id, error,
			    ESC_OTHER_MAIL_SYSTEM_STATUS);
			(void)snprintf(buf, sizeof buf,
			    "Error (%s%s%s)",
				       syserror ? syserror : "",
				       syserror ? ": " : "",
				       error);
			mda_log(e, "TempFail", buf);
			break;
		case MDA_PERMFAIL:
			mda_queue_permfail(e->id, error,
			    ESC_OTHER_MAIL_SYSTEM_STATUS);
			(void)snprintf(buf, sizeof buf,
			    "Error (%s%s%s)",
				       syserror ? syserror : "",
				       syserror ? ": " : "",
				       error);
			mda_log(e, "PermFail", buf);
			break;
		case MDA_OK:
			mda_queue_ok(e->id);
			mda_log(e, "Ok", "Delivered");
			break;
		}
		mda_done(s);
		return;
	}

	fatalx("mda_imsg: unexpected %s imsg", imsg_to_str(imsg->hdr.type));
}

void
mda_postfork(void)
{
}

void
mda_postprivdrop(void)
{
	tree_init(&sessions);
	tree_init(&users);
	TAILQ_INIT(&runnable);
}

static void
mda_io(struct io *io, int evt, void *arg)
{
	struct mda_session	*s = arg;
	char			*ln = NULL;
	size_t			 sz = 0;
	ssize_t			 len;

	log_trace(TRACE_IO, "mda: %p: %s %s", s, io_strevent(evt),
	    io_strio(io));

	switch (evt) {
	case IO_LOWAT:

	/* done */
	done:
		if (s->datafp == NULL) {
			log_debug("debug: mda: all data sent for session"
			    " %016"PRIx64 " evpid %016"PRIx64,
			    s->id, s->evp->id);
			io_free(io);
			s->io = NULL;
			return;
		}

		while (io_queued(s->io) < MDA_HIWAT) {
			if ((len = getline(&ln, &sz, s->datafp)) == -1)
				break;
			if (io_write(s->io, ln, len) == -1) {
				m_create(p_parent, IMSG_MDA_KILL,
				    0, 0, -1);
				m_add_id(p_parent, s->id);
				m_add_string(p_parent, "Out of memory");
				m_close(p_parent);
				io_pause(io, IO_OUT);
				free(ln);
				return;
			}
		}

		free(ln);
		ln = NULL;
		if (ferror(s->datafp)) {
			log_debug("debug: mda: ferror on session %016"PRIx64,
			    s->id);
			m_create(p_parent, IMSG_MDA_KILL, 0, 0, -1);
			m_add_id(p_parent, s->id);
			m_add_string(p_parent, "Error reading body");
			m_close(p_parent);
			io_pause(io, IO_OUT);
			return;
		}

		if (feof(s->datafp)) {
			log_debug("debug: mda: end-of-file for session"
			    " %016"PRIx64 " evpid %016"PRIx64,
			    s->id, s->evp->id);
			fclose(s->datafp);
			s->datafp = NULL;
			if (io_queued(s->io) == 0)
				goto done;
		}
		return;

	case IO_TIMEOUT:
		log_debug("debug: mda: timeout on session %016"PRIx64, s->id);
		io_pause(io, IO_OUT);
		return;

	case IO_ERROR:
		log_debug("debug: mda: io error on session %016"PRIx64": %s",
		    s->id, io_error(io));
		io_pause(io, IO_OUT);
		return;

	case IO_DISCONNECTED:
		log_debug("debug: mda: io disconnected on session %016"PRIx64,
		    s->id);
		io_pause(io, IO_OUT);
		return;

	default:
		log_debug("debug: mda: unexpected event on session %016"PRIx64,
		    s->id);
		io_pause(io, IO_OUT);
		return;
	}
}

static int
mda_check_loop(FILE *fp, struct mda_envelope *e)
{
	char		*buf = NULL;
	size_t		 sz = 0;
	ssize_t		 len;
	int		 ret = 0;

	while ((len = getline(&buf, &sz, fp)) != -1) {
		if (buf[len - 1] == '\n')
			buf[len - 1] = '\0';

		if (strchr(buf, ':') == NULL && !isspace((unsigned char)*buf))
			break;

		if (strncasecmp("Delivered-To: ", buf, 14) == 0) {
			if (strcasecmp(buf + 14, e->dest) == 0) {
				ret = 1;
				break;
			}
		}
	}

	free(buf);
	fseek(fp, SEEK_SET, 0);
	return (ret);
}

static int
mda_getlastline(int fd, char *dst, size_t dstsz)
{
	FILE	*fp;
	char	*ln = NULL;
	size_t	 sz = 0;
	ssize_t	 len;
	int	 out = 0;

	if (lseek(fd, 0, SEEK_SET) == -1) {
		log_warn("warn: mda: lseek");
		close(fd);
		return (-1);
	}
	fp = fdopen(fd, "r");
	if (fp == NULL) {
		log_warn("warn: mda: fdopen");
		close(fd);
		return (-1);
	}
	while ((len = getline(&ln, &sz, fp)) != -1) {
		if (ln[len - 1] == '\n')
			ln[len - 1] = '\0';
		out = 1;
	}
	fclose(fp);

	if (out) {
		(void)strlcpy(dst, "\"", dstsz);
		(void)strnvis(dst + 1, ln, dstsz - 2, VIS_SAFE | VIS_CSTYLE | VIS_NL);
		(void)strlcat(dst, "\"", dstsz);
	}

	free(ln);
	return (0);
}

static void
mda_fail(struct mda_user *user, int permfail, const char *error,
    enum enhanced_status_code code)
{
	struct mda_envelope	*e;

	while ((e = TAILQ_FIRST(&user->envelopes))) {
		TAILQ_REMOVE(&user->envelopes, e, entry);
		if (permfail) {
			mda_log(e, "PermFail", error);
			mda_queue_permfail(e->id, error, code);
		}
		else {
			mda_log(e, "TempFail", error);
			mda_queue_tempfail(e->id, error, code);
		}
		mda_envelope_free(e);
	}

	mda_user_free(user);
}

static void
mda_drain(void)
{
	struct mda_user		*u;

	while ((u = (TAILQ_FIRST(&runnable)))) {

		TAILQ_REMOVE(&runnable, u, entry_runnable);

		if (u->evpcount == 0 && u->running == 0) {
			log_debug("debug: mda: all done for user \"%s\"",
			    mda_user_to_text(u));
			mda_user_free(u);
			continue;
		}

		if (u->evpcount == 0) {
			log_debug("debug: mda: no more envelope for \"%s\"",
			    mda_user_to_text(u));
			u->flags &= ~USER_RUNNABLE;
			continue;
		}

		if (u->running >= env->sc_mda_max_user_session) {
			log_debug("debug: mda: "
			    "maximum number of session reached for user \"%s\"",
			    mda_user_to_text(u));
			u->flags &= ~USER_RUNNABLE;
			continue;
		}

		if (tree_count(&sessions) >= env->sc_mda_max_session) {
			log_debug("debug: mda: "
			    "maximum number of session reached");
			TAILQ_INSERT_HEAD(&runnable, u, entry_runnable);
			return;
		}

		mda_session(u);

		if (u->evpcount == env->sc_mda_task_lowat) {
			if (u->flags & USER_ONHOLD) {
				log_debug("debug: mda: down to lowat for user "
				    "\"%s\": releasing",
				    mda_user_to_text(u));
				u->flags &= ~USER_ONHOLD;
			}
			if (u->flags & USER_HOLDQ) {
				m_create(p_queue, IMSG_MDA_HOLDQ_RELEASE,
				    0, 0, -1);
				m_add_id(p_queue, u->id);
				m_add_int(p_queue, env->sc_mda_task_release);
				m_close(p_queue);
			}
		}

		/* re-add the user at the tail of the queue */
		TAILQ_INSERT_TAIL(&runnable, u, entry_runnable);
	}
}

static void
mda_done(struct mda_session *s)
{
	log_debug("debug: mda: session %016" PRIx64 " done", s->id);

	tree_xpop(&sessions, s->id);

	mda_envelope_free(s->evp);

	s->user->running--;
	if (!(s->user->flags & USER_RUNNABLE)) {
		log_debug("debug: mda: user \"%s\" becomes runnable",
		    s->user->name);
		TAILQ_INSERT_TAIL(&runnable, s->user, entry_runnable);
		s->user->flags |= USER_RUNNABLE;
	}

	if (s->datafp)
		fclose(s->datafp);
	if (s->io)
		io_free(s->io);

	free(s);

	stat_decrement("mda.running", 1);

	mda_drain();
}

static void
mda_log(const struct mda_envelope *evp, const char *prefix, const char *status)
{
	char rcpt[LINE_MAX];

	rcpt[0] = '\0';
	if (evp->rcpt)
		(void)snprintf(rcpt, sizeof rcpt, "rcpt=<%s> ", evp->rcpt);

	log_info("%016"PRIx64" mda delivery evpid=%016" PRIx64 " from=<%s> to=<%s> "
	    "%suser=%s delay=%s result=%s stat=%s",
	    evp->session_id,
	    evp->id,
	    evp->sender ? evp->sender : "",
	    evp->dest,
	    rcpt,
	    evp->user,
	    duration_to_text(time(NULL) - evp->creation),
	    prefix,
	    status);
}

static void
mda_queue_ok(uint64_t evpid)
{
	m_create(p_queue, IMSG_MDA_DELIVERY_OK, 0, 0, -1);
	m_add_evpid(p_queue, evpid);
	m_close(p_queue);
}

static void
mda_queue_tempfail(uint64_t evpid, const char *reason,
    enum enhanced_status_code code)
{
	m_create(p_queue, IMSG_MDA_DELIVERY_TEMPFAIL, 0, 0, -1);
	m_add_evpid(p_queue, evpid);
	m_add_string(p_queue, reason);
	m_add_int(p_queue, (int)code);
	m_close(p_queue);
}

static void
mda_queue_permfail(uint64_t evpid, const char *reason,
    enum enhanced_status_code code)
{
	m_create(p_queue, IMSG_MDA_DELIVERY_PERMFAIL, 0, 0, -1);
	m_add_evpid(p_queue, evpid);
	m_add_string(p_queue, reason);
	m_add_int(p_queue, (int)code);
	m_close(p_queue);
}

static void
mda_queue_loop(uint64_t evpid)
{
	m_create(p_queue, IMSG_MDA_DELIVERY_LOOP, 0, 0, -1);
	m_add_evpid(p_queue, evpid);
	m_close(p_queue);
}

static struct mda_user *
mda_user(const struct envelope *evp)
{
	struct dispatcher *dsp;
	struct mda_user	*u;
	void		*i;

	i = NULL;
	dsp = dict_xget(env->sc_dispatchers, evp->dispatcher);
	while (tree_iter(&users, &i, NULL, (void**)(&u))) {
		if (!strcmp(evp->mda_user, u->name) &&
		    !strcmp(dsp->u.local.table_userbase, u->usertable))
			return (u);
	}

	u = xcalloc(1, sizeof *u);
	u->id = generate_uid();
	TAILQ_INIT(&u->envelopes);
	(void)strlcpy(u->name, evp->mda_user, sizeof(u->name));
	(void)strlcpy(u->usertable, dsp->u.local.table_userbase,
	    sizeof(u->usertable));

	tree_xset(&users, u->id, u);

	m_create(p_lka, IMSG_MDA_LOOKUP_USERINFO, 0, 0, -1);
	m_add_id(p_lka, u->id);
	m_add_string(p_lka, dsp->u.local.table_userbase);
	m_add_string(p_lka, evp->mda_user);
	m_close(p_lka);
	u->flags |= USER_WAITINFO;

	stat_increment("mda.user", 1);

	if (dsp->u.local.user)
		log_debug("mda: new user %016" PRIx64
		    " for \"%s\" delivering as \"%s\"",
		    u->id, mda_user_to_text(u), dsp->u.local.user);
	else
		log_debug("mda: new user %016" PRIx64
		    " for \"%s\"", u->id, mda_user_to_text(u));

	return (u);
}

static void
mda_user_free(struct mda_user *u)
{
	tree_xpop(&users, u->id);

	if (u->flags & USER_HOLDQ) {
		m_create(p_queue, IMSG_MDA_HOLDQ_RELEASE, 0, 0, -1);
		m_add_id(p_queue, u->id);
		m_add_int(p_queue, 0);
		m_close(p_queue);
	}

	free(u);
	stat_decrement("mda.user", 1);
}

static const char *
mda_user_to_text(const struct mda_user *u)
{
	static char buf[1024];

	(void)snprintf(buf, sizeof(buf), "%s:%s", u->usertable, u->name);

	return (buf);
}

static struct mda_envelope *
mda_envelope(uint64_t session_id, const struct envelope *evp)
{
	struct mda_envelope	*e;
	char			 buf[LINE_MAX];

	e = xcalloc(1, sizeof *e);
	e->session_id = session_id;
	e->id = evp->id;
	e->creation = evp->creation;
	buf[0] = '\0';
	if (evp->sender.user[0] && evp->sender.domain[0])
		(void)snprintf(buf, sizeof buf, "%s@%s",
		    evp->sender.user, evp->sender.domain);
	e->sender = xstrdup(buf);
	(void)snprintf(buf, sizeof buf, "%s@%s", evp->dest.user,
	    evp->dest.domain);
	e->dest = xstrdup(buf);
	(void)snprintf(buf, sizeof buf, "%s@%s", evp->rcpt.user,
	    evp->rcpt.domain);
	e->rcpt = xstrdup(buf);
	e->user = evp->mda_user[0] ?
	    xstrdup(evp->mda_user) : xstrdup(evp->dest.user);
	e->dispatcher = xstrdup(evp->dispatcher);
	if (evp->mda_exec[0])
		e->mda_exec = xstrdup(evp->mda_exec);
	if (evp->mda_subaddress[0])
		e->mda_subaddress = xstrdup(evp->mda_subaddress);
	stat_increment("mda.envelope", 1);
	return (e);
}

static void
mda_envelope_free(struct mda_envelope *e)
{
	free(e->sender);
	free(e->dest);
	free(e->rcpt);
	free(e->user);
	free(e->mda_exec);
	free(e);

	stat_decrement("mda.envelope", 1);
}

static struct mda_session *
mda_session(struct mda_user * u)
{
	struct mda_session *s;

	s = xcalloc(1, sizeof *s);
	s->id = generate_uid();
	s->user = u;
	s->io = io_new();
	io_set_callback(s->io, mda_io, s);

	tree_xset(&sessions, s->id, s);

	s->evp = TAILQ_FIRST(&u->envelopes);
	TAILQ_REMOVE(&u->envelopes, s->evp, entry);
	u->evpcount--;
	u->running++;

	stat_decrement("mda.pending", 1);
	stat_increment("mda.running", 1);

	log_debug("debug: mda: new session %016" PRIx64
	    " for user \"%s\" evpid %016" PRIx64, s->id,
	    mda_user_to_text(u), s->evp->id);

	m_create(p_queue, IMSG_MDA_OPEN_MESSAGE, 0, 0, -1);
	m_add_id(p_queue, s->id);
	m_add_msgid(p_queue, evpid_to_msgid(s->evp->id));
	m_close(p_queue);

	return (s);
}

static const char *
mda_sysexit_to_str(int sysexit)
{
	switch (sysexit) {
	case EX_USAGE:
		return "command line usage error";
	case EX_DATAERR:
		return "data format error";
	case EX_NOINPUT:
		return "cannot open input";
	case EX_NOUSER:
		return "user unknown";
	case EX_NOHOST:
		return "host name unknown";
	case EX_UNAVAILABLE:
		return "service unavailable";
	case EX_SOFTWARE:
		return "internal software error";
	case EX_OSERR:
		return "system resource problem";
	case EX_OSFILE:
		return "critical OS file missing";
	case EX_CANTCREAT:
		return "can't create user output file";
	case EX_IOERR:
		return "input/output error";
	case EX_TEMPFAIL:
		return "temporary failure";
	case EX_PROTOCOL:
		return "remote error in protocol";
	case EX_NOPERM:
		return "permission denied";
	case EX_CONFIG:
		return "local configuration error";
	default:
		break;
	}
	return NULL;
}

