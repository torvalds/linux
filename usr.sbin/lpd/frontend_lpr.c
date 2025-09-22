/*	$OpenBSD: frontend_lpr.c,v 1.5 2024/11/21 13:34:51 claudio Exp $	*/

/*
 * Copyright (c) 2017 Eric Faurot <eric@openbsd.org>
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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "lpd.h"
#include "lp.h"

#include "io.h"
#include "log.h"
#include "proc.h"

#define SERVER_TIMEOUT	30000
#define CLIENT_TIMEOUT	 5000

#define	MAXARG	50

#define	F_ZOMBIE	0x1
#define	F_WAITADDRINFO	0x2

#define STATE_READ_COMMAND	0
#define STATE_READ_FILE		1

struct lpr_conn {
	SPLAY_ENTRY(lpr_conn)	 entry;
	uint32_t		 id;
	char			 hostname[NI_MAXHOST];
	struct io		*io;
	int			 state;
	int			 flags;
	int			 recvjob;
	int			 recvcf;
	size_t			 expect;
	FILE			*ofp;	/* output file when receiving data */
	int			 ifd;	/* input file for displayq/rmjob */

	char			*cmd;
	int			 ai_done;
	struct addrinfo		*ai;
	struct io		*iofwd;
};

SPLAY_HEAD(lpr_conn_tree, lpr_conn);

static int lpr_conn_cmp(struct lpr_conn *, struct lpr_conn *);
SPLAY_PROTOTYPE(lpr_conn_tree, lpr_conn, entry, lpr_conn_cmp);

static void lpr_on_allowedhost(struct lpr_conn *, const char *, const char *);
static void lpr_on_recvjob(struct lpr_conn *, int);
static void lpr_on_recvjob_file(struct lpr_conn *, int, size_t, int, int);
static void lpr_on_request(struct lpr_conn *, int, const char *, const char *);
static void lpr_on_getaddrinfo(void *, int, struct addrinfo *);

static void lpr_io_dispatch(struct io *, int, void *);
static int  lpr_readcommand(struct lpr_conn *);
static int  lpr_readfile(struct lpr_conn *);
static int  lpr_parsejobfilter(struct lpr_conn *, struct lp_jobfilter *,
    int, char **);

static void lpr_free(struct lpr_conn *);
static void lpr_close(struct lpr_conn *);
static void lpr_ack(struct lpr_conn *, char);
static void lpr_reply(struct lpr_conn *, const char *);
static void lpr_stream(struct lpr_conn *);
static void lpr_forward(struct lpr_conn *);

static void lpr_iofwd_dispatch(struct io *, int, void *);

static struct lpr_conn_tree conns;

void
lpr_init(void)
{
	SPLAY_INIT(&conns);
}

void
lpr_conn(uint32_t connid, struct listener *l, int sock,
    const struct sockaddr *sa)
{
	struct lpr_conn *conn;

	if ((conn = calloc(1, sizeof(*conn))) == NULL) {
		log_warn("%s: calloc", __func__);
		close(sock);
		frontend_conn_closed(connid);
		return;
	}
	conn->id = connid;
	conn->ifd = -1;
	conn->io = io_new();
	if (conn->io == NULL) {
		log_warn("%s: io_new", __func__);
		free(conn);
		close(sock);
		frontend_conn_closed(connid);
		return;
	}
	SPLAY_INSERT(lpr_conn_tree, &conns, conn);
	io_set_callback(conn->io, lpr_io_dispatch, conn);
	io_set_timeout(conn->io, CLIENT_TIMEOUT);
	io_set_write(conn->io);
	io_attach(conn->io, sock);

	conn->state = STATE_READ_COMMAND;
	m_create(p_engine, IMSG_LPR_ALLOWEDHOST, conn->id, 0, -1);
	m_add_sockaddr(p_engine, sa);
	m_close(p_engine);
}

void
lpr_dispatch_engine(struct imsgproc *proc, struct imsg *imsg)
{
	struct lpr_conn *conn = NULL, key;
	const char *hostname, *reject, *cmd;
	size_t sz;
	int ack, cf = 0;

	key.id = imsg->hdr.peerid;
	if (key.id) {
		conn = SPLAY_FIND(lpr_conn_tree, &conns, &key);
		if (conn == NULL) {
			log_debug("%08x dead-session", key.id);
			return;
		}
	}

	switch (imsg->hdr.type) {
	case IMSG_LPR_ALLOWEDHOST:
		m_get_string(proc, &hostname);
		m_get_string(proc, &reject);
		m_end(proc);
		lpr_on_allowedhost(conn, hostname, reject);
		break;

	case IMSG_LPR_RECVJOB:
		m_get_int(proc, &ack);
		m_end(proc);
		lpr_on_recvjob(conn, ack);
		break;

	case IMSG_LPR_RECVJOB_CF:
		cf = 1;
	case IMSG_LPR_RECVJOB_DF:
		m_get_int(proc, &ack);
		m_get_size(proc, &sz);
		m_end(proc);
		lpr_on_recvjob_file(conn, ack, sz, cf, imsg_get_fd(imsg));
		break;

	case IMSG_LPR_DISPLAYQ:
	case IMSG_LPR_RMJOB:
		m_get_string(proc, &hostname);
		m_get_string(proc, &cmd);
		m_end(proc);
		lpr_on_request(conn, imsg_get_fd(imsg), hostname, cmd);
		break;

	default:
		fatalx("%s: unexpected imsg %s", __func__,
		    log_fmt_imsgtype(imsg->hdr.type));
	}
}

static void
lpr_on_allowedhost(struct lpr_conn *conn, const char *hostname,
    const char *reject)
{
	strlcpy(conn->hostname, hostname, sizeof(conn->hostname));
	if (reject)
		lpr_reply(conn, reject);
	else
		io_set_read(conn->io);
}

static void
lpr_on_recvjob(struct lpr_conn *conn, int ack)
{
	if (ack == LPR_ACK)
		conn->recvjob = 1;
	else
		log_debug("%08x recvjob failed", conn->id);
	lpr_ack(conn, ack);
}

static void
lpr_on_recvjob_file(struct lpr_conn *conn, int ack, size_t sz, int cf, int fd)
{
	if (ack != LPR_ACK) {
		lpr_ack(conn, ack);
		return;
	}

	if (fd == -1) {
		log_warnx("%s: failed to get fd", __func__);
		lpr_ack(conn, LPR_NACK);
		return;
	}

	conn->ofp = fdopen(fd, "w");
	if (conn->ofp == NULL) {
		log_warn("%s: fdopen", __func__);
		close(fd);
		lpr_ack(conn, LPR_NACK);
		return;
	}

	conn->expect = sz;
	if (cf)
		conn->recvcf = cf;
	conn->state = STATE_READ_FILE;

	lpr_ack(conn, LPR_ACK);
}

static void
lpr_on_request(struct lpr_conn *conn, int fd, const char *hostname,
    const char *cmd)
{
	struct addrinfo hints;

	if (fd == -1) {
		log_warnx("%s: no fd received", __func__);
		lpr_close(conn);
		return;
	}

	log_debug("%08x stream init", conn->id);
	conn->ifd = fd;

	/* Prepare for command forwarding if necessary. */
	if (cmd) {
		log_debug("%08x forwarding to %s: \\%d%s", conn->id, hostname,
		    cmd[0], cmd + 1);
		conn->cmd = strdup(cmd);
		if (conn->cmd == NULL)
			log_warn("%s: strdup", __func__);
		else {
			memset(&hints, 0, sizeof(hints));
			hints.ai_socktype = SOCK_STREAM;
			conn->flags |= F_WAITADDRINFO;
			/*
			 * The callback might run immediately, so conn->ifd
			 * must be set before, to block lpr_forward().
			 */
			resolver_getaddrinfo(hostname, "printer", &hints,
			    lpr_on_getaddrinfo, conn);
		}
	}

	lpr_stream(conn);
}

static void
lpr_on_getaddrinfo(void *arg, int r, struct addrinfo *ai)
{
	struct lpr_conn *conn = arg;

	conn->flags &= ~F_WAITADDRINFO;
	if (conn->flags & F_ZOMBIE) {
		if (ai)
			freeaddrinfo(ai);
		lpr_free(conn);
	}
	else {
		conn->ai_done = 1;
		conn->ai = ai;
		lpr_forward(conn);
	}
}

static void
lpr_io_dispatch(struct io *io, int evt, void *arg)
{
	struct lpr_conn *conn = arg;
	int r;

	switch (evt) {
	case IO_DATAIN:
		switch(conn->state) {
		case STATE_READ_COMMAND:
			r = lpr_readcommand(conn);
			break;
		case STATE_READ_FILE:
			r = lpr_readfile(conn);
			break;
		default:
			fatal("%s: unexpected state %d", __func__, conn->state);
		}

		if (r == 0)
			io_set_write(conn->io);
		return;

	case IO_LOWAT:
		if (conn->recvjob)
			io_set_read(conn->io);
		else if (conn->ifd != -1)
			lpr_stream(conn);
		else if (conn->cmd == NULL)
			lpr_close(conn);
		return;

	case IO_DISCONNECTED:
		log_debug("%08x disconnected", conn->id);
		/*
		 * Some clients don't wait for the last acknowledgment to close
		 * the session.  So just consider it is closed normally.
		 */
	case IO_CLOSED:
		if (conn->recvcf && conn->state == STATE_READ_COMMAND) {
			/*
			 * Commit the transaction if we received a control file
			 * and the last file was received correctly.
			 */
			m_compose(p_engine, IMSG_LPR_RECVJOB_COMMIT, conn->id,
			    0, -1, NULL, 0);
			conn->recvjob = 0;
		}
		break;

	case IO_TIMEOUT:
		log_debug("%08x timeout", conn->id);
		break;

	case IO_ERROR:
		log_debug("%08x io-error", conn->id);
		break;

	default:
		fatalx("%s: unexpected event %d", __func__, evt);
	}

	lpr_close(conn);
}

static int
lpr_readcommand(struct lpr_conn *conn)
{
	struct lp_jobfilter jf;
	size_t count;
	const char *errstr;
	char *argv[MAXARG], *line;
	int i, argc, cmd;

	line = io_getline(conn->io, NULL);
	if (line == NULL) {
		if (io_datalen(conn->io) >= LPR_MAXCMDLEN) {
			lpr_reply(conn, "Request line too long");
			return 0;
		}
		return -1;
	}

	cmd = line[0];
	line++;

	if (cmd == 0) {
		lpr_reply(conn, "No command");
		return 0;
	}

	log_debug("%08x cmd \\%d", conn->id, cmd);

	/* Parse the command. */
	for (argc = 0; argc < MAXARG; ) {
		argv[argc] = strsep(&line, " \t");
		if (argv[argc] == NULL)
			break;
		if (argv[argc][0] != '\0')
			argc++;
	}
	if (argc == MAXARG) {
		lpr_reply(conn, "Argument list too long");
		return 0;
	}

	if (argc == 0) {
		lpr_reply(conn, "No queue specified");
		return 0;
	}

#define CMD(c)    ((int)(c))
#define SUBCMD(c) (0x100 | (int)(c))

	if (conn->recvjob)
		cmd |= 0x100;
	switch (cmd) {
	case CMD('\1'):	/* PRINT <prn> */
		m_create(p_engine, IMSG_LPR_PRINTJOB, 0, 0, -1);
		m_add_string(p_engine, argv[0]);
		m_close(p_engine);
		lpr_ack(conn, LPR_ACK);
		return 0;

	case CMD('\2'):	/* RECEIVE JOB <prn> */
		m_create(p_engine, IMSG_LPR_RECVJOB, conn->id, 0, -1);
		m_add_string(p_engine, conn->hostname);
		m_add_string(p_engine, argv[0]);
		m_close(p_engine);
		return 0;

	case CMD('\3'):	/* QUEUE STATE SHORT <prn> [job#...] [user..] */
	case CMD('\4'):	/* QUEUE STATE LONG  <prn> [job#...] [user..] */
		if (lpr_parsejobfilter(conn, &jf, argc - 1, argv + 1) == -1)
			return 0;

		m_create(p_engine, IMSG_LPR_DISPLAYQ, conn->id, 0, -1);
		m_add_int(p_engine, (cmd == '\3') ? 0 : 1);
		m_add_string(p_engine, conn->hostname);
		m_add_string(p_engine, argv[0]);
		m_add_int(p_engine, jf.njob);
		for (i = 0; i < jf.njob; i++)
			m_add_int(p_engine, jf.jobs[i]);
		m_add_int(p_engine, jf.nuser);
		for (i = 0; i < jf.nuser; i++)
			m_add_string(p_engine, jf.users[i]);
		m_close(p_engine);
		return 0;

	case CMD('\5'):	/* REMOVE JOBS <prn> <agent> [job#...] [user..] */
		if (argc < 2) {
			lpr_reply(conn, "No agent specified");
			return 0;
		}
		if (lpr_parsejobfilter(conn, &jf, argc - 2, argv + 2) == -1)
			return 0;

		m_create(p_engine, IMSG_LPR_RMJOB, conn->id, 0, -1);
		m_add_string(p_engine, conn->hostname);
		m_add_string(p_engine, argv[0]);
		m_add_string(p_engine, argv[1]);
		m_add_int(p_engine, jf.njob);
		for (i = 0; i < jf.njob; i++)
			m_add_int(p_engine, jf.jobs[i]);
		m_add_int(p_engine, jf.nuser);
		for (i = 0; i < jf.nuser; i++)
			m_add_string(p_engine, jf.users[i]);
		m_close(p_engine);
		return 0;

	case SUBCMD('\1'):	/* ABORT */
		m_compose(p_engine, IMSG_LPR_RECVJOB_CLEAR, conn->id, 0, -1,
		    NULL, 0);
		conn->recvcf = 0;
		lpr_ack(conn, LPR_ACK);
		return 0;

	case SUBCMD('\2'):	/* CONTROL FILE <size> <filename> */
	case SUBCMD('\3'):	/* DATA FILE    <size> <filename> */
		if (argc != 2) {
			log_debug("%08x invalid number of argument", conn->id);
			lpr_ack(conn, LPR_NACK);
			return 0;
		}
		errstr = NULL;
		count = strtonum(argv[0], 1, LPR_MAXFILESIZE, &errstr);
		if (errstr) {
			log_debug("%08x invalid file size: %s", conn->id,
			    strerror(errno));
			lpr_ack(conn, LPR_NACK);
			return 0;
		}

		if (cmd == SUBCMD('\2')) {
			if (conn->recvcf) {
				log_debug("%08x cf file already received",
				    conn->id);
				lpr_ack(conn, LPR_NACK);
				return 0;
			}
			m_create(p_engine, IMSG_LPR_RECVJOB_CF, conn->id, 0,
			    -1);
		}
		else
			m_create(p_engine, IMSG_LPR_RECVJOB_DF, conn->id, 0,
			    -1);
		m_add_size(p_engine, count);
		m_add_string(p_engine, argv[1]);
		m_close(p_engine);
		return 0;

	default:
		if (conn->recvjob)
			lpr_reply(conn, "Protocol error");
		else
			lpr_reply(conn, "Illegal service request");
		return 0;
	}
}

static int
lpr_readfile(struct lpr_conn *conn)
{
	size_t len, w;
	char *data;

	if (conn->expect) {
		/* Read file content. */
		data = io_data(conn->io);
		len = io_datalen(conn->io);
		if (len > conn->expect)
			len = conn->expect;

		log_debug("%08x %zu bytes received", conn->id, len);

		w = fwrite(data, 1, len, conn->ofp);
		if (w != len) {
			log_warnx("%s: fwrite", __func__);
			lpr_close(conn);
			return -1;
		}
		io_drop(conn->io, w);
		conn->expect -= w;
		if (conn->expect)
			return -1;

		fclose(conn->ofp);
		conn->ofp = NULL;

		log_debug("%08x file received", conn->id);
	}

	/* Try to read '\0'. */
	len = io_datalen(conn->io);
	if (len == 0)
		return -1;
	data = io_data(conn->io);
	io_drop(conn->io, 1);

	log_debug("%08x eof %d", conn->id, (int)*data);

	if (*data != '\0') {
		lpr_close(conn);
		return -1;
	}

	conn->state = STATE_READ_COMMAND;
	lpr_ack(conn, LPR_ACK);
	return 0;
}

static int
lpr_parsejobfilter(struct lpr_conn *conn, struct lp_jobfilter *jf, int argc,
    char **argv)
{
	const char *errstr;
	char *arg;
	int i, jobnum;

	memset(jf, 0, sizeof(*jf));

	for (i = 0; i < argc; i++) {
		arg = argv[i];
		if (isdigit((unsigned char)arg[0])) {
			if (jf->njob == LP_MAXREQUESTS) {
				lpr_reply(conn, "Too many requests");
				return -1;
			}
			errstr = NULL;
			jobnum = strtonum(arg, 0, INT_MAX, &errstr);
			if (errstr) {
				lpr_reply(conn, "Invalid job number");
				return -1;
			}
			jf->jobs[jf->njob++] = jobnum;
		}
		else {
			if (jf->nuser == LP_MAXUSERS) {
				lpr_reply(conn, "Too many users");
				return -1;
			}
			jf->users[jf->nuser++] = arg;
		}
	}

	return 0;
}

static void
lpr_free(struct lpr_conn *conn)
{
	if ((conn->flags & F_WAITADDRINFO) == 0)
		free(conn);
}

static void
lpr_close(struct lpr_conn *conn)
{
	uint32_t connid = conn->id;

	SPLAY_REMOVE(lpr_conn_tree, &conns, conn);

	if (conn->recvjob)
		m_compose(p_engine, IMSG_LPR_RECVJOB_ROLLBACK, conn->id, 0, -1,
		    NULL, 0);

	io_free(conn->io);
	free(conn->cmd);
	if (conn->ofp)
		fclose(conn->ofp);
	if (conn->ifd != -1)
		close(conn->ifd);
	if (conn->ai)
		freeaddrinfo(conn->ai);
	if (conn->iofwd)
		io_free(conn->iofwd);

	conn->flags |= F_ZOMBIE;
	lpr_free(conn);

	frontend_conn_closed(connid);
}

static void
lpr_ack(struct lpr_conn *conn, char c)
{
	if (c == 0)
		log_debug("%08x ack", conn->id);
	else
		log_debug("%08x nack %d", conn->id, (int)c);

	io_write(conn->io, &c, 1);
}

static void
lpr_reply(struct lpr_conn *conn, const char *s)
{
	log_debug("%08x reply: %s", conn->id, s);

	io_printf(conn->io, "%s\n", s);
}

/*
 * Stream response file to the client.
 */
static void
lpr_stream(struct lpr_conn *conn)
{
	char buf[BUFSIZ];
	ssize_t r;

	for (;;) {
		if (io_queued(conn->io) > 65536)
			return;

		r = read(conn->ifd, buf, sizeof(buf));
		if (r == -1) {
			if (errno == EINTR)
				continue;
			log_warn("%s: read", __func__);
			break;
		}

		if (r == 0) {
			log_debug("%08x stream done", conn->id);
			break;
		}
		log_debug("%08x stream %zu bytes", conn->id, r);

		if (io_write(conn->io, buf, r) == -1) {
			log_warn("%s: io_write", __func__);
			break;
		}
	}

	close(conn->ifd);
	conn->ifd = -1;

	if (conn->cmd)
		lpr_forward(conn);

	else if (io_queued(conn->io) == 0)
		lpr_close(conn);
}

/*
 * Forward request to the remote printer.
 */
static void
lpr_forward(struct lpr_conn *conn)
{
	/*
	 * Do not start forwarding the command if the address is not resolved
	 * or if the local response is still being sent to the client.
	 */
	if (!conn->ai_done || conn->ifd == -1)
		return;

	if (conn->ai == NULL) {
		if (io_queued(conn->io) == 0)
			lpr_close(conn);
		return;
	}

	log_debug("%08x forward start", conn->id);

	conn->iofwd = io_new();
	if (conn->iofwd == NULL) {
		log_warn("%s: io_new", __func__);
		if (io_queued(conn->io) == 0)
			lpr_close(conn);
		return;
	}
	io_set_callback(conn->iofwd, lpr_iofwd_dispatch, conn);
	io_set_timeout(conn->io, SERVER_TIMEOUT);
	io_connect(conn->iofwd, conn->ai);
	conn->ai = NULL;
}

static void
lpr_iofwd_dispatch(struct io *io, int evt, void *arg)
{
	struct lpr_conn *conn = arg;

	switch (evt) {
	case IO_CONNECTED:
		log_debug("%08x forward connected", conn->id);
		/* Send the request. */
		io_print(io, conn->cmd);
		io_print(io, "\n");
		io_set_write(io);
		return;

	case IO_DATAIN:
		/* Relay. */
		io_write(conn->io, io_data(io), io_datalen(io));
		io_drop(io, io_datalen(io));
		return;

	case IO_LOWAT:
		/* Read response. */
		io_set_read(io);
		return;

	case IO_CLOSED:
		break;

	case IO_DISCONNECTED:
		log_debug("%08x forward disconnected", conn->id);
		break;

	case IO_TIMEOUT:
		log_debug("%08x forward timeout", conn->id);
		break;

	case IO_ERROR:
		log_debug("%08x forward io-error", conn->id);
		break;

	default:
		fatalx("%s: unexpected event %d", __func__, evt);
	}

	log_debug("%08x forward done", conn->id);

	io_free(io);
	free(conn->cmd);
	conn->cmd = NULL;
	conn->iofwd = NULL;
	if (io_queued(conn->io) == 0)
		lpr_close(conn);
}

static int
lpr_conn_cmp(struct lpr_conn *a, struct lpr_conn *b)
{
	if (a->id < b->id)
		return -1;
	if (a->id > b->id)
		return 1;
	return 0;
}

SPLAY_GENERATE(lpr_conn_tree, lpr_conn, entry, lpr_conn_cmp);
