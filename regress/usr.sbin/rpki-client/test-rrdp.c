/*	$OpenBSD: test-rrdp.c,v 1.11 2025/07/23 07:21:32 tb Exp $ */
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

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <sha2.h>

#include <expat.h>
#include <openssl/sha.h>

#include "extern.h"
#include "rrdp.h"

int filemode;
int outformats;
int verbose;
int experimental;

#define REGRESS_NOTIFY_URI	"https://rpki.example.com/notify.xml"

#define MAX_SESSIONS	12
#define	READ_BUF_SIZE	(32 * 1024)

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
	unsigned int		 file_pending;
	unsigned int		 file_failed;
	enum http_result	 res;
	enum rrdp_task		 task;

	char			 hash[SHA256_DIGEST_LENGTH];
	SHA256_CTX		 ctx;

	struct rrdp_session	 repository;
	struct rrdp_session	 current;
	XML_Parser		 parser;
	struct notification_xml	*nxml;
	struct snapshot_xml	*sxml;
	struct delta_xml	*dxml;
};

void
logx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarnx(fmt, ap);
	va_end(ap);
}

char *
xstrdup(const char *s)
{
	char *r;
	if ((r = strdup(s)) == NULL)
		err(1, "strdup");
	return r;
}

/*
 * Send a blob of data to the main process to store it in the repository.
 */
void
rrdp_publish_file(struct rrdp *s, struct publish_xml *pxml,
    unsigned char *data, size_t datasz)
{
	char buf[SHA256_DIGEST_STRING_LENGTH];
	char *hash = NULL;

	switch (pxml->type) {
	case PUB_ADD:
		logx("type: %s", "add");
		break;
	case PUB_UPD:
		logx("type: %s", "update");
		hash = hex_encode(pxml->hash, sizeof(pxml->hash));
		break;
	case PUB_DEL:
		logx("type: %s", "delete");
		hash = hex_encode(pxml->hash, sizeof(pxml->hash));
		break;
	default:
		errx(1, "unknown publish type");
	}
	logx("uri: %s", pxml->uri);
	SHA256Data(data, datasz, buf);
	logx("data: %s", buf);

	if (hash)
		logx("hash: %s", hash);
	free(hash);
}

static struct rrdp *
rrdp_new(unsigned int id, char *local, char *notify, char *session_id,
    long long serial, char *last_mod)
{
	struct rrdp *s;

	if ((s = calloc(1, sizeof(*s))) == NULL)
		err(1, NULL);

	s->infd = 0; /* stdin */
	s->id = id;
	if ((s->local = strdup(local)) == NULL)
		err(1, NULL);
	if ((s->notifyuri = strdup(notify)) == NULL)
		err(1, NULL);
	if (session_id != NULL &&
	    (s->repository.session_id = strdup(session_id)) == NULL)
		err(1, NULL);
	s->repository.serial = serial;
	if (last_mod != NULL &&
	    (s->repository.last_mod = strdup(last_mod)) == NULL)
		err(1, NULL);

	s->state = RRDP_STATE_REQ;
	if ((s->parser = XML_ParserCreate("US-ASCII")) == NULL)
		err(1, "XML_ParserCreate");

	return s;
}

static void
rrdp_free(struct rrdp *s)
{
	if (s == NULL)
		return;

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
	free(s->repository.last_mod);
	free(s->repository.session_id);
	free(s->current.last_mod);
	free(s->current.session_id);

	free(s);
}

static void
rrdp_finished(struct rrdp *s)
{
	XML_Parser p = s->parser;

	if (s->state & RRDP_STATE_PARSE_ERROR)
		return;

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
		return;
	}

	switch (s->task) {
	case NOTIFICATION:
		notification_done(s->nxml, NULL);
		log_notification_xml(s->nxml);
		break;
	case SNAPSHOT:
		log_snapshot_xml(s->sxml);
		break;
	case DELTA:
		log_delta_xml(s->dxml);
		break;
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
		s->state |= RRDP_STATE_PARSE_ERROR;
		warn("%s: read failure", s->local);
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

int
main(int argc, char **argv)
{
	struct rrdp *s = NULL;
	const char *e;
	char *session_id = NULL;
	char hash[SHA256_DIGEST_LENGTH];
	long long serial = 0;
	int c, ret = 0;


	while ((c = getopt(argc, argv, "dH:N:nS:s")) != -1)
		switch (c) {
		case 'd':
			if (s)
				goto usage;
			s = rrdp_new(0, "stdin", REGRESS_NOTIFY_URI,
			    session_id, serial, NULL);
			s->dxml = new_delta_xml(s->parser,
			    &s->repository, s);
			s->task = DELTA;
			SHA256_Init(&s->ctx);
			memcpy(s->hash, hash, sizeof(s->hash));
			break;
		case 'H':
			if (hex_decode(optarg, hash, sizeof(hash)) == -1)
				errx(1, "bad hash");
			break;
		case 'N':
			serial = strtonum(optarg, LLONG_MIN, LLONG_MAX, &e);
			if (e != NULL)
				errx(1, "serial is %s: %s", e, optarg);
			break;
		case 'n':
			if (s)
				goto usage;
			s = rrdp_new(0, "stdin", REGRESS_NOTIFY_URI,
			    session_id, serial, NULL);
			s->nxml = new_notification_xml(s->parser,
			    &s->repository, &s->current, s->notifyuri);
			s->task = NOTIFICATION;
			break;
		case 'S':
			session_id = optarg;
			break;
		case 's':
			if (s)
				goto usage;
			s = rrdp_new(0, "stdin", REGRESS_NOTIFY_URI,
			    session_id, serial, NULL);
			s->sxml = new_snapshot_xml(s->parser,
			    &s->repository, s);
			s->task = SNAPSHOT;
			SHA256_Init(&s->ctx);
			memcpy(s->hash, hash, sizeof(s->hash));
			break;
		default:
			goto usage;
		}

	s->state = RRDP_STATE_PARSE;

	while (!(s->state & RRDP_STATE_PARSE_DONE)) {
		rrdp_data_handler(s);
	}

	if ((ret = (s->state & RRDP_STATE_PARSE_ERROR)) == 0)
		printf("OK\n");

	rrdp_free(s);

	return ret;

usage:
	fprintf(stderr, "usage: %s [-S session_id] [-N serial] [-H hash] "
	    "-d | -n | -s\n", "test-rrdp");
	exit(1);
}

time_t
get_current_time(void)
{
	return time(NULL);
}
