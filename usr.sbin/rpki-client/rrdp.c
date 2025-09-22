/*	$OpenBSD: rrdp.c,v 1.41 2025/08/01 13:46:06 claudio Exp $ */
/*
 * Copyright (c) 2020 Nils Fisher <nils_fisher@hotmail.com>
 * Copyright (c) 2021 Claudio Jeker <claudio@openbsd.org>
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
#include <sys/queue.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <imsg.h>

#include <expat.h>
#include <openssl/sha.h>

#include "extern.h"
#include "rrdp.h"

#define MAX_SESSIONS	32
#define	READ_BUF_SIZE	(32 * 1024)

static struct msgbuf	*msgq;

#define RRDP_STATE_REQ		0x01
#define RRDP_STATE_WAIT		0x02
#define RRDP_STATE_PARSE	0x04
#define RRDP_STATE_PARSE_ERROR	0x08
#define RRDP_STATE_PARSE_DONE	0x10
#define RRDP_STATE_HTTP_DONE	0x20
#define RRDP_STATE_DONE		(RRDP_STATE_PARSE_DONE | RRDP_STATE_HTTP_DONE)

struct rrdp {
	TAILQ_ENTRY(rrdp)	 entry;
	unsigned int		 id;
	char			*notifyuri;
	char			*local;
	char			*last_mod;

	struct pollfd		*pfd;
	int			 infd;
	int			 state;
	int			 aborted;
	unsigned int		 file_pending;
	unsigned int		 file_failed;
	enum http_result	 res;
	enum rrdp_task		 task;

	char			 hash[SHA256_DIGEST_LENGTH];
	SHA256_CTX		 ctx;

	struct rrdp_session	*repository;
	struct rrdp_session	*current;
	XML_Parser		 parser;
	struct notification_xml	*nxml;
	struct snapshot_xml	*sxml;
	struct delta_xml	*dxml;
};

static TAILQ_HEAD(, rrdp)	states = TAILQ_HEAD_INITIALIZER(states);

char *
xstrdup(const char *s)
{
	char *r;
	if ((r = strdup(s)) == NULL)
		err(1, "strdup");
	return r;
}

/*
 * Report back that a RRDP request finished.
 * ok should only be set to 1 if the cache is now up-to-date.
 */
static void
rrdp_done(unsigned int id, int ok)
{
	enum rrdp_msg type = RRDP_END;
	struct ibuf *b;

	b = io_new_buffer();
	io_simple_buffer(b, &type, sizeof(type));
	io_simple_buffer(b, &id, sizeof(id));
	io_simple_buffer(b, &ok, sizeof(ok));
	io_close_buffer(msgq, b);
}

/*
 * Request an URI to be fetched via HTTPS.
 * The main process will respond with a RRDP_HTTP_INI which includes
 * the file descriptor to read from. RRDP_HTTP_FIN is sent at the
 * end of the request with the HTTP status code and last modified timestamp.
 * If the request should not set the If-Modified-Since: header then last_mod
 * should be set to NULL, else it should point to a proper date string.
 */
static void
rrdp_http_req(unsigned int id, const char *uri, const char *last_mod)
{
	enum rrdp_msg type = RRDP_HTTP_REQ;
	struct ibuf *b;

	b = io_new_buffer();
	io_simple_buffer(b, &type, sizeof(type));
	io_simple_buffer(b, &id, sizeof(id));
	io_str_buffer(b, uri);
	io_opt_str_buffer(b, last_mod);
	io_close_buffer(msgq, b);
}

/*
 * Send the session state to the main process so it gets stored.
 */
static void
rrdp_state_send(struct rrdp *s)
{
	enum rrdp_msg type = RRDP_SESSION;
	struct ibuf *b;

	b = io_new_buffer();
	io_simple_buffer(b, &type, sizeof(type));
	io_simple_buffer(b, &s->id, sizeof(s->id));
	rrdp_session_buffer(b, s->current);
	io_close_buffer(msgq, b);
}

/*
 * Inform parent to clear the RRDP repository before start of snapshot.
 */
static void
rrdp_clear_repo(struct rrdp *s)
{
	enum rrdp_msg type = RRDP_CLEAR;
	struct ibuf *b;

	b = io_new_buffer();
	io_simple_buffer(b, &type, sizeof(type));
	io_simple_buffer(b, &s->id, sizeof(s->id));
	io_close_buffer(msgq, b);
}

/*
 * Send a blob of data to the main process to store it in the repository.
 */
void
rrdp_publish_file(struct rrdp *s, struct publish_xml *pxml,
    unsigned char *data, size_t datasz)
{
	enum rrdp_msg type = RRDP_FILE;
	struct ibuf *b;

	/* only send files if the fetch did not fail already */
	if (s->file_failed == 0) {
		b = io_new_buffer();
		io_simple_buffer(b, &type, sizeof(type));
		io_simple_buffer(b, &s->id, sizeof(s->id));
		io_simple_buffer(b, &pxml->type, sizeof(pxml->type));
		if (pxml->type != PUB_ADD)
			io_simple_buffer(b, &pxml->hash, sizeof(pxml->hash));
		io_str_buffer(b, pxml->uri);
		io_buf_buffer(b, data, datasz);
		io_close_buffer(msgq, b);
		s->file_pending++;
	}
}

static void
rrdp_new(unsigned int id, char *local, char *notify, struct rrdp_session *state)
{
	struct rrdp *s;

	if ((s = calloc(1, sizeof(*s))) == NULL)
		err(1, NULL);

	s->infd = -1;
	s->id = id;
	s->local = local;
	s->notifyuri = notify;
	s->repository = state;
	if ((s->current = calloc(1, sizeof(*s->current))) == NULL)
		err(1, NULL);

	s->state = RRDP_STATE_REQ;
	if ((s->parser = XML_ParserCreate("US-ASCII")) == NULL)
		err(1, "XML_ParserCreate");

	s->nxml = new_notification_xml(s->parser, s->repository, s->current,
	    notify);

	TAILQ_INSERT_TAIL(&states, s, entry);
}

static void
rrdp_free(struct rrdp *s)
{
	if (s == NULL)
		return;

	TAILQ_REMOVE(&states, s, entry);

	free_notification_xml(s->nxml);
	free_snapshot_xml(s->sxml);
	free_delta_xml(s->dxml);

	if (s->parser)
		XML_ParserFree(s->parser);
	if (s->infd != -1)
		close(s->infd);
	free(s->notifyuri);
	free(s->local);
	free(s->last_mod);
	rrdp_session_free(s->repository);
	rrdp_session_free(s->current);

	free(s);
}

static struct rrdp *
rrdp_get(unsigned int id)
{
	struct rrdp *s;

	TAILQ_FOREACH(s, &states, entry)
		if (s->id == id)
			break;
	return s;
}

static void
rrdp_failed(struct rrdp *s)
{
	unsigned int id = s->id;

	/* reset file state before retrying */
	s->file_failed = 0;

	if (s->task == DELTA && !s->aborted) {
		/* fallback to a snapshot as per RFC8182 */
		free_delta_xml(s->dxml);
		s->dxml = NULL;
		rrdp_clear_repo(s);
		s->sxml = new_snapshot_xml(s->parser, s->current, s);
		s->task = SNAPSHOT;
		s->state = RRDP_STATE_REQ;
		logx("%s: delta sync failed, fallback to snapshot", s->local);
	} else {
		/*
		 * TODO: update state to track recurring failures
		 * and fall back to rsync after a while.
		 * This should probably happen in the main process.
		 */
		rrdp_free(s);
		rrdp_done(id, 0);
	}
}

static void
rrdp_finished(struct rrdp *s)
{
	unsigned int id = s->id;

	/* check if all parts of the process have finished */
	if ((s->state & RRDP_STATE_DONE) != RRDP_STATE_DONE)
		return;

	/* still some files pending */
	if (s->file_pending > 0)
		return;

	if (s->state & RRDP_STATE_PARSE_ERROR || s->aborted) {
		rrdp_failed(s);
		return;
	}

	if (s->res == HTTP_OK) {
		XML_Parser p = s->parser;

		/*
		 * Finalize parsing on success to be sure that
		 * all of the XML is correct. Needs to be done here
		 * since the call would most probably fail for non
		 * successful data fetches.
		 */
		if (XML_Parse(p, NULL, 0, 1) != XML_STATUS_OK) {
			warnx("%s: XML error at line %llu: %s", s->local,
			    (unsigned long long)XML_GetCurrentLineNumber(p),
			    XML_ErrorString(XML_GetErrorCode(p)));
			rrdp_failed(s);
			return;
		}

		/* If a file caused an error fail the update */
		if (s->file_failed > 0) {
			rrdp_failed(s);
			return;
		}

		switch (s->task) {
		case NOTIFICATION:
			s->task = notification_done(s->nxml, s->last_mod);
			s->last_mod = NULL;
			switch (s->task) {
			case NOTIFICATION:
				logx("%s: repository not modified (%s#%lld)",
				    s->local, s->repository->session_id,
				    s->repository->serial);
				/* no need to update state file */
				rrdp_free(s);
				rrdp_done(id, 1);
				break;
			case SNAPSHOT:
				logx("%s: downloading snapshot (%s#%lld)",
				    s->local, s->current->session_id,
				    s->current->serial);
				rrdp_clear_repo(s);
				s->sxml = new_snapshot_xml(p, s->current, s);
				s->state = RRDP_STATE_REQ;
				break;
			case DELTA:
				logx("%s: downloading %lld deltas (%s#%lld)",
				    s->local,
				    s->repository->serial - s->current->serial,
				    s->current->session_id, s->current->serial);
				s->dxml = new_delta_xml(p, s->current, s);
				s->state = RRDP_STATE_REQ;
				break;
			}
			break;
		case SNAPSHOT:
			rrdp_state_send(s);
			rrdp_free(s);
			rrdp_done(id, 1);
			break;
		case DELTA:
			if (notification_delta_done(s->nxml)) {
				/* finished */
				rrdp_state_send(s);
				rrdp_free(s);
				rrdp_done(id, 1);
			} else {
				/* reset delta parser for next delta */
				free_delta_xml(s->dxml);
				s->dxml = new_delta_xml(p, s->current, s);
				s->state = RRDP_STATE_REQ;
			}
			break;
		}
	} else if (s->res == HTTP_NOT_MOD && s->task == NOTIFICATION) {
		logx("%s: notification file not modified (%s#%lld)", s->local,
		    s->repository->session_id, s->repository->serial);
		/* no need to update state file */
		rrdp_free(s);
		rrdp_done(id, 1);
	} else {
		rrdp_failed(s);
	}
}

static void
rrdp_abort_req(struct rrdp *s)
{
	unsigned int id = s->id;

	s->aborted = 1;
	if (s->state == RRDP_STATE_REQ) {
		/* nothing is pending, just abort */
		rrdp_free(s);
		rrdp_done(id, 1);
		return;
	}
	if (s->state == RRDP_STATE_WAIT)
		/* wait for HTTP_INI which will progress the state */
		return;

	/*
	 * RRDP_STATE_PARSE or later, close infd, abort parser but
	 * wait for HTTP_FIN and file_pending to drop to 0.
	 */
	if (s->infd != -1) {
		close(s->infd);
		s->infd = -1;
		s->state |= RRDP_STATE_PARSE_DONE | RRDP_STATE_PARSE_ERROR;
	}
	rrdp_finished(s);
}

static void
rrdp_input_handler(struct ibuf *b)
{
	struct rrdp_session *state;
	char *local, *notify, *last_mod;
	struct rrdp *s;
	enum rrdp_msg type;
	enum http_result res;
	unsigned int id;
	int ok;

	io_read_buf(b, &type, sizeof(type));
	io_read_buf(b, &id, sizeof(id));

	switch (type) {
	case RRDP_START:
		if (ibuf_fd_avail(b))
			errx(1, "received unexpected fd");
		io_read_str(b, &local);
		io_read_str(b, &notify);
		io_read_buf(b, &ok, sizeof(ok));
		if (ok != 0) {
			state = rrdp_session_read(b);
		} else {
			if ((state = calloc(1, sizeof(*state))) == NULL)
				err(1, NULL);
		}
		rrdp_new(id, local, notify, state);
		break;
	case RRDP_HTTP_INI:
		s = rrdp_get(id);
		if (s == NULL)
			errx(1, "http ini, rrdp session %u does not exist", id);
		if (s->state != RRDP_STATE_WAIT)
			errx(1, "%s: bad internal state", s->local);
		s->infd = ibuf_fd_get(b);
		if (s->infd == -1)
			errx(1, "expected fd not received");
		s->state = RRDP_STATE_PARSE;
		if (s->aborted) {
			rrdp_abort_req(s);
			break;
		}
		break;
	case RRDP_HTTP_FIN:
		io_read_buf(b, &res, sizeof(res));
		io_read_opt_str(b, &last_mod);
		if (ibuf_fd_avail(b))
			errx(1, "received unexpected fd");

		s = rrdp_get(id);
		if (s == NULL)
			errx(1, "http fin, rrdp session %u does not exist", id);
		if (!(s->state & RRDP_STATE_PARSE))
			errx(1, "%s: bad internal state", s->local);
		s->state |= RRDP_STATE_HTTP_DONE;
		s->res = res;
		free(s->last_mod);
		s->last_mod = last_mod;
		rrdp_finished(s);
		break;
	case RRDP_FILE:
		s = rrdp_get(id);
		if (s == NULL)
			errx(1, "file, rrdp session %u does not exist", id);
		if (ibuf_fd_avail(b))
			errx(1, "received unexpected fd");
		io_read_buf(b, &ok, sizeof(ok));
		if (ok != 1)
			s->file_failed++;
		s->file_pending--;
		if (s->file_pending == 0)
			rrdp_finished(s);
		break;
	case RRDP_ABORT:
		if (ibuf_fd_avail(b))
			errx(1, "received unexpected fd");
		s = rrdp_get(id);
		if (s != NULL)
			rrdp_abort_req(s);
		break;
	default:
		errx(1, "unexpected message %d", type);
	}
}

static void
rrdp_data_handler(struct rrdp *s)
{
	char buf[READ_BUF_SIZE];
	XML_Parser p = s->parser;
	ssize_t len;

	len = read(s->infd, buf, sizeof(buf));
	if (len == -1) {
		warn("%s: read failure", s->local);
		rrdp_abort_req(s);
		return;
	}
	if ((s->state & RRDP_STATE_PARSE) == 0)
		errx(1, "%s: bad parser state", s->local);
	if (len == 0) {
		/* parser stage finished */
		close(s->infd);
		s->infd = -1;

		if (s->task != NOTIFICATION) {
			char h[SHA256_DIGEST_LENGTH];

			SHA256_Final(h, &s->ctx);
			if (memcmp(s->hash, h, sizeof(s->hash)) != 0) {
				s->state |= RRDP_STATE_PARSE_ERROR;
				warnx("%s: bad message digest", s->local);
			}
		}

		s->state |= RRDP_STATE_PARSE_DONE;
		rrdp_finished(s);
		return;
	}

	/* parse and maybe hash the bytes just read */
	if (s->task != NOTIFICATION)
		SHA256_Update(&s->ctx, buf, len);
	if ((s->state & RRDP_STATE_PARSE_ERROR) == 0 &&
	    XML_Parse(p, buf, len, 0) != XML_STATUS_OK) {
		warnx("%s: parse error at line %llu: %s", s->local,
		    (unsigned long long)XML_GetCurrentLineNumber(p),
		    XML_ErrorString(XML_GetErrorCode(p)));
		s->state |= RRDP_STATE_PARSE_ERROR;
	}
}

void
proc_rrdp(int fd)
{
	struct pollfd pfds[MAX_SESSIONS + 1];
	struct rrdp *s, *ns;
	struct ibuf *b;
	size_t i;

	if (pledge("stdio recvfd", NULL) == -1)
		err(1, "pledge");

	if ((msgq = msgbuf_new_reader(sizeof(size_t), io_parse_hdr, NULL)) ==
	    NULL)
		err(1, NULL);

	for (;;) {
		i = 1;
		memset(&pfds, 0, sizeof(pfds));
		TAILQ_FOREACH(s, &states, entry) {
			if (i >= MAX_SESSIONS + 1) {
				/* not enough sessions, wait for better times */
				s->pfd = NULL;
				continue;
			}
			/* request new assets when there are free sessions */
			if (s->state == RRDP_STATE_REQ) {
				const char *uri;
				switch (s->task) {
				case NOTIFICATION:
					rrdp_http_req(s->id, s->notifyuri,
					    s->repository->last_mod);
					break;
				case SNAPSHOT:
				case DELTA:
					uri = notification_get_next(s->nxml,
					    s->hash, sizeof(s->hash),
					    s->task);
					SHA256_Init(&s->ctx);
					rrdp_http_req(s->id, uri, NULL);
					break;
				}
				s->state = RRDP_STATE_WAIT;
			}
			s->pfd = pfds + i++;
			s->pfd->fd = s->infd;
			s->pfd->events = POLLIN;
		}

		/*
		 * Update main fd last.
		 * The previous loop may have enqueue messages.
		 */
		pfds[0].fd = fd;
		pfds[0].events = POLLIN;
		if (msgbuf_queuelen(msgq) > 0)
			pfds[0].events |= POLLOUT;

		if (poll(pfds, i, INFTIM) == -1) {
			if (errno == EINTR)
				continue;
			err(1, "poll");
		}

		if (pfds[0].revents & POLLHUP)
			break;
		if (pfds[0].revents & POLLOUT) {
			if (msgbuf_write(fd, msgq) == -1) {
				if (errno == EPIPE)
					errx(1, "write: connection closed");
				else
					err(1, "write");
			}
		}
		if (pfds[0].revents & POLLIN) {
			switch (msgbuf_read(fd, msgq)) {
			case -1:
				err(1, "msgbuf_read");
			case 0:
				errx(1, "msgbuf_read: connection closed");
			}
			while ((b = io_buf_get(msgq)) != NULL) {
				rrdp_input_handler(b);
				ibuf_free(b);
			}
		}

		TAILQ_FOREACH_SAFE(s, &states, entry, ns) {
			if (s->pfd == NULL)
				continue;
			if (s->pfd->revents != 0)
				rrdp_data_handler(s);
		}
	}

	exit(0);
}
