/*	$OpenBSD: smtpctl.c,v 1.176 2024/11/21 13:42:22 claudio Exp $	*/

/*
 * Copyright (c) 2013 Eric Faurot <eric@openbsd.org>
 * Copyright (c) 2006 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2006 Pierre-Yves Ritschard <pyr@openbsd.org>
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2003 Henning Brauer <henning@openbsd.org>
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

#include <sys/un.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fts.h>
#include <inttypes.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <vis.h>

#include "smtpd.h"
#include "parser.h"
#include "log.h"

#define PATH_GZCAT	"/usr/bin/gzcat"
#define	PATH_CAT	"/bin/cat"
#define PATH_QUEUE	"/queue"
#define PATH_ENCRYPT	"/usr/bin/encrypt"

int srv_connect(void);
int srv_connected(void);

void usage(void);
static void show_queue_envelope(struct envelope *, int);
static void getflag(uint *, int, char *, char *, size_t);
static void display(const char *);
static int str_to_trace(const char *);
static int str_to_profile(const char *);
static void show_offline_envelope(uint64_t);
static int is_gzip_fp(FILE *);
static int is_encrypted_fp(FILE *);
static int is_encrypted_buffer(const char *);
static int is_gzip_buffer(const char *);
static FILE *offline_file(void);
static void sendmail_compat(int, char **);

extern int	spfwalk(int, struct parameter *);

extern char	*__progname;
int		 sendmail;
struct smtpd	*env;
struct imsgbuf	*ibuf;
struct imsg	 imsg;
char		*rdata;
size_t		 rlen;
time_t		 now;

struct queue_backend queue_backend_null;
struct queue_backend queue_backend_proc;
struct queue_backend queue_backend_ram;

__dead void
usage(void)
{
	if (sendmail)
		fprintf(stderr, "usage: %s [-tv] [-f from] [-F name] to ...\n",
		    __progname);
	else
		fprintf(stderr, "usage: %s command [argument ...]\n",
		    __progname);
	exit(1);
}

void stat_increment(const char *k, size_t v)
{
}

void stat_decrement(const char *k, size_t v)
{
}

int
srv_connect(void)
{
	struct sockaddr_un	s_un;
	int			ctl_sock, saved_errno;

	/* connect to smtpd control socket */
	if ((ctl_sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "socket");

	memset(&s_un, 0, sizeof(s_un));
	s_un.sun_family = AF_UNIX;
	(void)strlcpy(s_un.sun_path, SMTPD_SOCKET, sizeof(s_un.sun_path));
	if (connect(ctl_sock, (struct sockaddr *)&s_un, sizeof(s_un)) == -1) {
		saved_errno = errno;
		close(ctl_sock);
		errno = saved_errno;
		return (0);
	}

	ibuf = xcalloc(1, sizeof(struct imsgbuf));
	if (imsgbuf_init(ibuf, ctl_sock) == -1)
		err(1, "imsgbuf_init");
	imsgbuf_allow_fdpass(ibuf);

	return (1);
}

int
srv_connected(void)
{
	return ibuf != NULL ? 1 : 0;
}

FILE *
offline_file(void)
{
	char	path[PATH_MAX];
	int	fd;
	FILE   *fp;

	if (!bsnprintf(path, sizeof(path), "%s%s/%lld.XXXXXXXXXX", PATH_SPOOL,
	    PATH_OFFLINE, (long long)time(NULL)))
		err(EX_UNAVAILABLE, "snprintf");

	if ((fd = mkstemp(path)) == -1 || (fp = fdopen(fd, "w+")) == NULL) {
		if (fd != -1)
			unlink(path);
		err(EX_UNAVAILABLE, "cannot create temporary file %s", path);
	}

	if (fchmod(fd, 0600) == -1) {
		unlink(path);
		err(EX_SOFTWARE, "fchmod");
	}

	return fp;
}


static void
srv_flush(void)
{
	if (imsgbuf_flush(ibuf) == -1)
		err(1, "write error");
}

static void
srv_send(int msg, const void *data, size_t len)
{
	if (ibuf == NULL && !srv_connect())
		errx(1, "smtpd doesn't seem to be running");
	imsg_compose(ibuf, msg, IMSG_VERSION, 0, -1, data, len);
}

static void
srv_recv(int type)
{
	ssize_t	n;

	srv_flush();

	while (1) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			errx(1, "imsg_get error");
		if (n) {
			if (imsg.hdr.type == IMSG_CTL_FAIL &&
			    imsg.hdr.peerid != 0 &&
			    imsg.hdr.peerid != IMSG_VERSION)
				errx(1, "incompatible smtpctl and smtpd");
			if (type != -1 && type != (int)imsg.hdr.type)
				errx(1, "bad message type");
			rdata = imsg.data;
			rlen = imsg.hdr.len - sizeof(imsg.hdr);
			break;
		}

		if ((n = imsgbuf_read(ibuf)) == -1)
			err(1, "read error");
		if (n == 0)
			errx(1, "pipe closed");
	}
}

static void
srv_read(void *dst, size_t sz)
{
	if (sz == 0)
		return;
	if (rlen < sz)
		errx(1, "message too short");
	if (dst)
		memmove(dst, rdata, sz);
	rlen -= sz;
	rdata += sz;
}

static void
srv_get_int(int *i)
{
	srv_read(i, sizeof(*i));
}

static void
srv_get_time(time_t *t)
{
	srv_read(t, sizeof(*t));
}

static void
srv_get_evpid(uint64_t *evpid)
{
	srv_read(evpid, sizeof(*evpid));
}

static void
srv_get_string(const char **s)
{
	const char *end;
	size_t len;

	if (rlen == 0)
		errx(1, "message too short");

	rlen -= 1;
	if (*rdata++ == '\0') {
		*s = NULL;
		return;
	}

	if (rlen == 0)
		errx(1, "bogus string");

	end = memchr(rdata, 0, rlen);
	if (end == NULL)
		errx(1, "unterminated string");

	len = end + 1 - rdata;

	*s = rdata;
	rlen -= len;
	rdata += len;
}

static void
srv_get_envelope(struct envelope *evp)
{
	uint64_t	 evpid;
	const char	*str;

	srv_get_evpid(&evpid);
	srv_get_string(&str);

	envelope_load_buffer(evp, str, strlen(str));
	evp->id = evpid;
}

static void
srv_end(void)
{
	if (rlen)
		errx(1, "bogus data");
	imsg_free(&imsg);
}

static int
srv_check_result(int verbose_)
{
	srv_recv(-1);
	srv_end();

	switch (imsg.hdr.type) {
	case IMSG_CTL_OK:
		if (verbose_)
			printf("command succeeded\n");
		return (0);
	case IMSG_CTL_FAIL:
		if (verbose_) {
			if (rlen)
				printf("command failed: %s\n", rdata);
			else
				printf("command failed\n");
		}
		return (1);
	default:
		errx(1, "wrong message in response: %u", imsg.hdr.type);
	}
	return (0);
}

static int
srv_iter_messages(uint32_t *res)
{
	static uint32_t	*msgids = NULL, from = 0;
	static size_t	 n, curr;
	static int	 done = 0;

	if (done)
		return (0);

	if (msgids == NULL) {
		srv_send(IMSG_CTL_LIST_MESSAGES, &from, sizeof(from));
		srv_recv(IMSG_CTL_LIST_MESSAGES);
		if (rlen == 0) {
			srv_end();
			done = 1;
			return (0);
		}
		msgids = malloc(rlen);
		n = rlen / sizeof(*msgids);
		srv_read(msgids, rlen);
		srv_end();

		curr = 0;
		from = msgids[n - 1] + 1;
		if (from == 0)
			done = 1;
	}

	*res = msgids[curr++];
	if (curr == n) {
		free(msgids);
		msgids = NULL;
	}

	return (1);
}

static int
srv_iter_envelopes(uint32_t msgid, struct envelope *evp)
{
	static uint32_t	currmsgid = 0;
	static uint64_t	from = 0;
	static int	done = 0, need_send = 1, found;
	int		flags;
	time_t		nexttry;

	if (currmsgid != msgid) {
		if (currmsgid != 0 && !done)
			errx(1, "must finish current iteration first");
		currmsgid = msgid;
		from = msgid_to_evpid(msgid);
		done = 0;
		found = 0;
		need_send = 1;
	}

	if (done)
		return (0);

    again:
	if (need_send) {
		found = 0;
		srv_send(IMSG_CTL_LIST_ENVELOPES, &from, sizeof(from));
	}
	need_send = 0;

	srv_recv(IMSG_CTL_LIST_ENVELOPES);
	if (rlen == 0) {
		srv_end();
		if (!found || evpid_to_msgid(from) != msgid) {
			done = 1;
			return (0);
		}
		need_send = 1;
		goto again;
	}

	srv_get_int(&flags);
	srv_get_time(&nexttry);
	srv_get_envelope(evp);
	srv_end();

	evp->flags |= flags;
	evp->nexttry = nexttry;

	from = evp->id + 1;
	found++;
	return (1);
}

static int
srv_iter_evpids(uint32_t msgid, uint64_t *evpid, int *offset)
{
	static uint64_t	*evpids = NULL, *tmp;
	static int	 n, tmpalloc, alloc = 0;
	struct envelope	 evp;

	if (*offset == 0) {
		n = 0;
		while (srv_iter_envelopes(msgid, &evp)) {
			if (n == alloc) {
				tmpalloc = alloc ? (alloc * 2) : 128;
				tmp = recallocarray(evpids, alloc, tmpalloc,
				    sizeof(*evpids));
				if (tmp == NULL)
					err(1, "recallocarray");
				evpids = tmp;
				alloc = tmpalloc;
			}
			evpids[n++] = evp.id;
		}
	}

	if (*offset >= n)
		return (0);
	*evpid = evpids[*offset];
	*offset += 1;
	return (1);
}

static void
srv_foreach_envelope(struct parameter *argv, int ctl, size_t *total, size_t *ok)
{
	uint32_t	msgid;
	uint64_t	evpid;
	int		i;

	*total = 0;
	*ok = 0;

	if (argv == NULL) {
		while (srv_iter_messages(&msgid)) {
			i = 0;
			while (srv_iter_evpids(msgid, &evpid, &i)) {
				*total += 1;
				srv_send(ctl, &evpid, sizeof(evpid));
				if (srv_check_result(0) == 0)
					*ok += 1;
			}
		}
	} else if (argv->type == P_MSGID) {
		i = 0;
		while (srv_iter_evpids(argv->u.u_msgid, &evpid, &i)) {
			srv_send(ctl, &evpid, sizeof(evpid));
			if (srv_check_result(0) == 0)
				*ok += 1;
		}
	} else {
		*total += 1;
		srv_send(ctl, &argv->u.u_evpid, sizeof(evpid));
		if (srv_check_result(0) == 0)
			*ok += 1;
	}
}

static void
srv_show_cmd(int cmd, const void *data, size_t len)
{
	int	done = 0;

	srv_send(cmd, data, len);

	do {
		srv_recv(cmd);
		if (rlen) {
			printf("%s\n", rdata);
			srv_read(NULL, rlen);
		}
		else
			done = 1;
		srv_end();
	} while (!done);
}

static void
droppriv(void)
{
	struct passwd *pw;

	if (geteuid())
		return;

	if ((pw = getpwnam(SMTPD_USER)) == NULL)
		errx(1, "unknown user " SMTPD_USER);

	if ((setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid)))
		err(1, "cannot drop privileges");
}

static int
do_permission_denied(int argc, struct parameter *argv)
{
	errx(1, "need root privileges");
}

static int
do_log_brief(int argc, struct parameter *argv)
{
	int	v = 0;

	srv_send(IMSG_CTL_VERBOSE, &v, sizeof(v));
	return srv_check_result(1);
}

static int
do_log_verbose(int argc, struct parameter *argv)
{
	int	v = TRACE_DEBUG;

	srv_send(IMSG_CTL_VERBOSE, &v, sizeof(v));
	return srv_check_result(1);
}

static int
do_monitor(int argc, struct parameter *argv)
{
	struct stat_digest	last, digest;
	size_t			count;

	memset(&last, 0, sizeof(last));
	count = 0;

	while (1) {
		srv_send(IMSG_CTL_GET_DIGEST, NULL, 0);
		srv_recv(IMSG_CTL_GET_DIGEST);
		srv_read(&digest, sizeof(digest));
		srv_end();

		if (count % 25 == 0) {
			if (count != 0)
				printf("\n");
			printf("--- client ---  "
			    "-- envelope --   "
			    "---- relay/delivery --- "
			    "------- misc -------\n"
			    "curr conn disc  "
			    "curr  enq  deq   "
			    "ok tmpfail prmfail loop "
			    "expire remove bounce\n");
		}
		printf("%4zu %4zu %4zu  "
		    "%4zu %4zu %4zu "
		    "%4zu    %4zu    %4zu %4zu   "
		    "%4zu   %4zu   %4zu\n",
		    digest.clt_connect - digest.clt_disconnect,
		    digest.clt_connect - last.clt_connect,
		    digest.clt_disconnect - last.clt_disconnect,

		    digest.evp_enqueued - digest.evp_dequeued,
		    digest.evp_enqueued - last.evp_enqueued,
		    digest.evp_dequeued - last.evp_dequeued,

		    digest.dlv_ok - last.dlv_ok,
		    digest.dlv_tempfail - last.dlv_tempfail,
		    digest.dlv_permfail - last.dlv_permfail,
		    digest.dlv_loop - last.dlv_loop,

		    digest.evp_expired - last.evp_expired,
		    digest.evp_removed - last.evp_removed,
		    digest.evp_bounce - last.evp_bounce);

		last = digest;
		count++;
		sleep(1);
	}

	return (0);
}

static int
do_pause_envelope(int argc, struct parameter *argv)
{
	size_t	total, ok;

	srv_foreach_envelope(argv, IMSG_CTL_PAUSE_EVP, &total, &ok);
	printf("%zu envelope%s paused\n", ok, (ok > 1) ? "s" : "");

	return (0);
}

static int
do_pause_mda(int argc, struct parameter *argv)
{
	srv_send(IMSG_CTL_PAUSE_MDA, NULL, 0);
	return srv_check_result(1);
}

static int
do_pause_mta(int argc, struct parameter *argv)
{
	srv_send(IMSG_CTL_PAUSE_MTA, NULL, 0);
	return srv_check_result(1);
}

static int
do_pause_smtp(int argc, struct parameter *argv)
{
	srv_send(IMSG_CTL_PAUSE_SMTP, NULL, 0);
	return srv_check_result(1);
}

static int
do_profile(int argc, struct parameter *argv)
{
	int	v;

	v = str_to_profile(argv[0].u.u_str);

	srv_send(IMSG_CTL_PROFILE_ENABLE, &v, sizeof(v));
	return srv_check_result(1);
}

static int
do_remove(int argc, struct parameter *argv)
{
	size_t	total, ok;

	srv_foreach_envelope(argv, IMSG_CTL_REMOVE, &total, &ok);
	printf("%zu envelope%s removed\n", ok, (ok > 1) ? "s" : "");

	return (0);
}

static int
do_resume_envelope(int argc, struct parameter *argv)
{
	size_t	total, ok;

	srv_foreach_envelope(argv, IMSG_CTL_RESUME_EVP, &total, &ok);
	printf("%zu envelope%s resumed\n", ok, (ok > 1) ? "s" : "");

	return (0);
}

static int
do_resume_mda(int argc, struct parameter *argv)
{
	srv_send(IMSG_CTL_RESUME_MDA, NULL, 0);
	return srv_check_result(1);
}

static int
do_resume_mta(int argc, struct parameter *argv)
{
	srv_send(IMSG_CTL_RESUME_MTA, NULL, 0);
	return srv_check_result(1);
}

static int
do_resume_route(int argc, struct parameter *argv)
{
	uint64_t	v;

	if (argc == 0)
		v = 0;
	else
		v = argv[0].u.u_routeid;

	srv_send(IMSG_CTL_RESUME_ROUTE, &v, sizeof(v));
	return srv_check_result(1);
}

static int
do_resume_smtp(int argc, struct parameter *argv)
{
	srv_send(IMSG_CTL_RESUME_SMTP, NULL, 0);
	return srv_check_result(1);
}

static int
do_schedule(int argc, struct parameter *argv)
{
	size_t	total, ok;

	srv_foreach_envelope(argv, IMSG_CTL_SCHEDULE, &total, &ok);
	printf("%zu envelope%s scheduled\n", ok, (ok > 1) ? "s" : "");

	return (0);
}

static int
do_show_envelope(int argc, struct parameter *argv)
{
	char	 buf[PATH_MAX];

	if (!bsnprintf(buf, sizeof(buf), "%s%s/%02x/%08x/%016" PRIx64,
	    PATH_SPOOL,
	    PATH_QUEUE,
	    (evpid_to_msgid(argv[0].u.u_evpid) & 0xff000000) >> 24,
	    evpid_to_msgid(argv[0].u.u_evpid),
	    argv[0].u.u_evpid))
		errx(1, "unable to retrieve envelope");

	display(buf);

	return (0);
}

static int
do_show_hoststats(int argc, struct parameter *argv)
{
	srv_show_cmd(IMSG_CTL_MTA_SHOW_HOSTSTATS, NULL, 0);

	return (0);
}

static int
do_show_message(int argc, struct parameter *argv)
{
	char	 buf[PATH_MAX];
	uint32_t msgid;

	if (argv[0].type == P_EVPID)
		msgid = evpid_to_msgid(argv[0].u.u_evpid);
	else
		msgid = argv[0].u.u_msgid;

	if (!bsnprintf(buf, sizeof(buf), "%s%s/%02x/%08x/message",
	    PATH_SPOOL,
	    PATH_QUEUE,
	    (msgid & 0xff000000) >> 24,
	    msgid))
		errx(1, "unable to retrieve message");

	display(buf);

	return (0);
}

static int
do_show_queue(int argc, struct parameter *argv)
{
	struct envelope	 evp;
	uint32_t	 msgid;
	FTS		*fts;
	FTSENT		*ftse;
	char		*qpath[] = {"/queue", NULL};
	char		*tmp;
	uint64_t	 evpid;

	now = time(NULL);

	if (!srv_connect()) {
		queue_init("fs", 0);
		if (chroot(PATH_SPOOL) == -1 || chdir("/") == -1)
			err(1, "%s", PATH_SPOOL);
		fts = fts_open(qpath, FTS_PHYSICAL|FTS_NOCHDIR, NULL);
		if (fts == NULL)
			err(1, "%s/queue", PATH_SPOOL);

		while ((ftse = fts_read(fts)) != NULL) {
			switch (ftse->fts_info) {
			case FTS_DP:
			case FTS_DNR:
				break;
			case FTS_F:
				tmp = NULL;
				evpid = strtoull(ftse->fts_name, &tmp, 16);
				if (tmp && *tmp != '\0')
					break;
				show_offline_envelope(evpid);
			}
		}

		fts_close(fts);
		return (0);
	}

	if (argc == 0) {
		msgid = 0;
		while (srv_iter_messages(&msgid))
			while (srv_iter_envelopes(msgid, &evp))
				show_queue_envelope(&evp, 1);
	} else if (argv[0].type == P_MSGID) {
		while (srv_iter_envelopes(argv[0].u.u_msgid, &evp))
			show_queue_envelope(&evp, 1);
	}

	return (0);
}

static int
do_show_hosts(int argc, struct parameter *argv)
{
	srv_show_cmd(IMSG_CTL_MTA_SHOW_HOSTS, NULL, 0);

	return (0);
}

static int
do_show_relays(int argc, struct parameter *argv)
{
	srv_show_cmd(IMSG_CTL_MTA_SHOW_RELAYS, NULL, 0);

	return (0);
}

static int
do_show_routes(int argc, struct parameter *argv)
{
	srv_show_cmd(IMSG_CTL_MTA_SHOW_ROUTES, NULL, 0);

	return (0);
}

static int
do_show_stats(int argc, struct parameter *argv)
{
	struct stat_kv	kv;
	time_t		duration;

	memset(&kv, 0, sizeof kv);

	while (1) {
		srv_send(IMSG_CTL_GET_STATS, &kv, sizeof kv);
		srv_recv(IMSG_CTL_GET_STATS);
		srv_read(&kv, sizeof(kv));
		srv_end();

		if (kv.iter == NULL)
			break;

		if (strcmp(kv.key, "uptime") == 0) {
			duration = time(NULL) - kv.val.u.counter;
			printf("uptime=%lld\n", (long long)duration);
			printf("uptime.human=%s\n",
			    duration_to_text(duration));
		}
		else {
			switch (kv.val.type) {
			case STAT_COUNTER:
				printf("%s=%zd\n",
				    kv.key, kv.val.u.counter);
				break;
			case STAT_TIMESTAMP:
				printf("%s=%" PRId64 "\n",
				    kv.key, (int64_t)kv.val.u.timestamp);
				break;
			case STAT_TIMEVAL:
				printf("%s=%lld.%lld\n",
				    kv.key, (long long)kv.val.u.tv.tv_sec,
				    (long long)kv.val.u.tv.tv_usec);
				break;
			case STAT_TIMESPEC:
				printf("%s=%lld.%06ld\n",
				    kv.key,
				    (long long)kv.val.u.ts.tv_sec * 1000000 +
				    kv.val.u.ts.tv_nsec / 1000000,
				    kv.val.u.ts.tv_nsec % 1000000);
				break;
			}
		}
	}

	return (0);
}

static int
do_show_status(int argc, struct parameter *argv)
{
	uint32_t	sc_flags;

	srv_send(IMSG_CTL_SHOW_STATUS, NULL, 0);
	srv_recv(IMSG_CTL_SHOW_STATUS);
	srv_read(&sc_flags, sizeof(sc_flags));
	srv_end();
	printf("MDA %s\n",
	    (sc_flags & SMTPD_MDA_PAUSED) ? "paused" : "running");
	printf("MTA %s\n",
	    (sc_flags & SMTPD_MTA_PAUSED) ? "paused" : "running");
	printf("SMTP %s\n",
	    (sc_flags & SMTPD_SMTP_PAUSED) ? "paused" : "running");
	return (0);
}

static int
do_trace(int argc, struct parameter *argv)
{
	int	v;

	v = str_to_trace(argv[0].u.u_str);

	srv_send(IMSG_CTL_TRACE_ENABLE, &v, sizeof(v));
	return srv_check_result(1);
}

static int
do_unprofile(int argc, struct parameter *argv)
{
	int	v;

	v = str_to_profile(argv[0].u.u_str);

	srv_send(IMSG_CTL_PROFILE_DISABLE, &v, sizeof(v));
	return srv_check_result(1);
}

static int
do_untrace(int argc, struct parameter *argv)
{
	int	v;

	v = str_to_trace(argv[0].u.u_str);

	srv_send(IMSG_CTL_TRACE_DISABLE, &v, sizeof(v));
	return srv_check_result(1);
}

static int
do_update_table(int argc, struct parameter *argv)
{
	const char	*name = argv[0].u.u_str;

	srv_send(IMSG_CTL_UPDATE_TABLE, name, strlen(name) + 1);
	return srv_check_result(1);
}

static int
do_encrypt(int argc, struct parameter *argv)
{
	const char *p = NULL;

	droppriv();

	if (argv)
		p = argv[0].u.u_str;
	execl(PATH_ENCRYPT, "encrypt", "--", p, (char *)NULL);
	errx(1, "execl");
}

static int
do_block_mta(int argc, struct parameter *argv)
{
	struct ibuf *m;

	if (ibuf == NULL && !srv_connect())
		errx(1, "smtpd doesn't seem to be running");
	m = imsg_create(ibuf, IMSG_CTL_MTA_BLOCK, IMSG_VERSION, 0,
	    sizeof(argv[0].u.u_ss) + strlen(argv[1].u.u_str) + 1);
	if (imsg_add(m, &argv[0].u.u_ss, sizeof(argv[0].u.u_ss)) == -1)
		errx(1, "imsg_add");
	if (imsg_add(m, argv[1].u.u_str, strlen(argv[1].u.u_str) + 1) == -1)
		errx(1, "imsg_add");
	imsg_close(ibuf, m);

	return srv_check_result(1);
}

static int
do_unblock_mta(int argc, struct parameter *argv)
{
	struct ibuf *m;

	if (ibuf == NULL && !srv_connect())
		errx(1, "smtpd doesn't seem to be running");

	m = imsg_create(ibuf, IMSG_CTL_MTA_UNBLOCK, IMSG_VERSION, 0,
	    sizeof(argv[0].u.u_ss) + strlen(argv[1].u.u_str) + 1);
	if (imsg_add(m, &argv[0].u.u_ss, sizeof(argv[0].u.u_ss)) == -1)
		errx(1, "imsg_add");
	if (imsg_add(m, argv[1].u.u_str, strlen(argv[1].u.u_str) + 1) == -1)
		errx(1, "imsg_add");
	imsg_close(ibuf, m);

	return srv_check_result(1);
}

static int
do_show_mta_block(int argc, struct parameter *argv)
{
	srv_show_cmd(IMSG_CTL_MTA_SHOW_BLOCK, NULL, 0);

	return (0);
}

static int
do_discover(int argc, struct parameter *argv)
{
	uint64_t evpid;
	uint32_t msgid;
	size_t	 n_evp;

	if (ibuf == NULL && !srv_connect())
		errx(1, "smtpd doesn't seem to be running");

	if (argv[0].type == P_EVPID) {
		evpid = argv[0].u.u_evpid;
		srv_send(IMSG_CTL_DISCOVER_EVPID, &evpid, sizeof evpid);
		srv_recv(IMSG_CTL_DISCOVER_EVPID);
	} else {
		msgid = argv[0].u.u_msgid;
		srv_send(IMSG_CTL_DISCOVER_MSGID, &msgid, sizeof msgid);
		srv_recv(IMSG_CTL_DISCOVER_MSGID);
	}

	if (rlen == 0) {
		srv_end();
		return (0);
	} else {
		srv_read(&n_evp, sizeof n_evp);
		srv_end();
	}

	printf("%zu envelope%s discovered\n", n_evp, (n_evp != 1) ? "s" : "");
	return (0);
}

static int
do_spf_walk(int argc, struct parameter *argv)
{
	droppriv();

	return spfwalk(argc, argv);
}

#define cmd_install_priv(s, f) \
	cmd_install((s), privileged ? (f) : do_permission_denied)

int
main(int argc, char **argv)
{
	gid_t		 gid;
	int		 privileged;
	char		*argv_mailq[] = { "show", "queue", NULL };

	log_init(1, LOG_MAIL);

	sendmail_compat(argc, argv);
	privileged = geteuid() == 0;

	gid = getgid();
	if (setresgid(gid, gid, gid) == -1)
		err(1, "setresgid");

	/* Privileged commands */
	cmd_install_priv("discover <evpid>",	do_discover);
	cmd_install_priv("discover <msgid>",	do_discover);
	cmd_install_priv("pause mta from <addr> for <str>", do_block_mta);
	cmd_install_priv("resume mta from <addr> for <str>", do_unblock_mta);
	cmd_install_priv("show mta paused",	do_show_mta_block);
	cmd_install_priv("log brief",		do_log_brief);
	cmd_install_priv("log verbose",		do_log_verbose);
	cmd_install_priv("monitor",		do_monitor);
	cmd_install_priv("pause envelope <evpid>", do_pause_envelope);
	cmd_install_priv("pause envelope <msgid>", do_pause_envelope);
	cmd_install_priv("pause envelope all",	do_pause_envelope);
	cmd_install_priv("pause mda",		do_pause_mda);
	cmd_install_priv("pause mta",		do_pause_mta);
	cmd_install_priv("pause smtp",		do_pause_smtp);
	cmd_install_priv("profile <str>",	do_profile);
	cmd_install_priv("remove <evpid>",	do_remove);
	cmd_install_priv("remove <msgid>",	do_remove);
	cmd_install_priv("remove all",		do_remove);
	cmd_install_priv("resume envelope <evpid>", do_resume_envelope);
	cmd_install_priv("resume envelope <msgid>", do_resume_envelope);
	cmd_install_priv("resume envelope all",	do_resume_envelope);
	cmd_install_priv("resume mda",		do_resume_mda);
	cmd_install_priv("resume mta",		do_resume_mta);
	cmd_install_priv("resume route <routeid>", do_resume_route);
	cmd_install_priv("resume smtp",		do_resume_smtp);
	cmd_install_priv("schedule <msgid>",	do_schedule);
	cmd_install_priv("schedule <evpid>",	do_schedule);
	cmd_install_priv("schedule all",	do_schedule);
	cmd_install_priv("show envelope <evpid>", do_show_envelope);
	cmd_install_priv("show hoststats",	do_show_hoststats);
	cmd_install_priv("show message <msgid>", do_show_message);
	cmd_install_priv("show message <evpid>", do_show_message);
	cmd_install_priv("show queue",		do_show_queue);
	cmd_install_priv("show queue <msgid>",	do_show_queue);
	cmd_install_priv("show hosts",		do_show_hosts);
	cmd_install_priv("show relays",		do_show_relays);
	cmd_install_priv("show routes",		do_show_routes);
	cmd_install_priv("show stats",		do_show_stats);
	cmd_install_priv("show status",		do_show_status);
	cmd_install_priv("trace <str>",		do_trace);
	cmd_install_priv("unprofile <str>",	do_unprofile);
	cmd_install_priv("untrace <str>",	do_untrace);
	cmd_install_priv("update table <str>",	do_update_table);

	/* Unprivileged commands */
	cmd_install("encrypt",			do_encrypt);
	cmd_install("encrypt <str>",		do_encrypt);
	cmd_install("spf walk",			do_spf_walk);

	if (strcmp(__progname, "mailq") == 0)
		return cmd_run(2, argv_mailq);
	if (strcmp(__progname, "smtpctl") == 0)
		return cmd_run(argc - 1, argv + 1);

	errx(1, "unsupported mode");
	return (0);
}

void
sendmail_compat(int argc, char **argv)
{
	FILE	*offlinefp = NULL;
	gid_t	 gid;
	int	 i, r;

	if (strcmp(__progname, "sendmail") == 0 ||
	    strcmp(__progname, "send-mail") == 0) {
		/*
		 * determine whether we are called with flags
		 * that should invoke makemap/newaliases.
		 */
		for (i = 1; i < argc; i++)
			if (strncmp(argv[i], "-bi", 3) == 0)
				exit(makemap(P_SENDMAIL, argc, argv));

		if (!srv_connect())
			offlinefp = offline_file();

		gid = getgid();
		if (setresgid(gid, gid, gid) == -1)
			err(1, "setresgid");

		/* we'll reduce further down the road */
		if (pledge("stdio rpath wpath cpath tmppath flock "
			"dns getpw recvfd", NULL) == -1)
			err(1, "pledge");

		sendmail = 1;
		exit(enqueue(argc, argv, offlinefp));
	} else if (strcmp(__progname, "makemap") == 0)
		exit(makemap(P_MAKEMAP, argc, argv));
	else if (strcmp(__progname, "newaliases") == 0) {
		r = makemap(P_NEWALIASES, argc, argv);
		/*
		 * if server is available, notify of table update.
		 * only makes sense for static tables AND if server is up.
		 */
		if (srv_connect()) {
			srv_send(IMSG_CTL_UPDATE_TABLE, "aliases", strlen("aliases") + 1);
			srv_check_result(0);
		}
		exit(r);
	}
}

static void
show_queue_envelope(struct envelope *e, int online)
{
	const char	*src = "?", *agent = "?";
	char		 status[128], runstate[128], errline[LINE_MAX];

	status[0] = '\0';

	getflag(&e->flags, EF_BOUNCE, "bounce", status, sizeof(status));
	getflag(&e->flags, EF_AUTHENTICATED, "auth", status, sizeof(status));
	getflag(&e->flags, EF_INTERNAL, "internal", status, sizeof(status));
	getflag(&e->flags, EF_SUSPEND, "suspend", status, sizeof(status));
	getflag(&e->flags, EF_HOLD, "hold", status, sizeof(status));

	if (online) {
		if (e->flags & EF_PENDING)
			(void)snprintf(runstate, sizeof runstate, "pending|%zd",
			    (ssize_t)(e->nexttry - now));
		else if (e->flags & EF_INFLIGHT)
			(void)snprintf(runstate, sizeof runstate,
			    "inflight|%zd", (ssize_t)(now - e->lasttry));
		else
			(void)snprintf(runstate, sizeof runstate, "invalid|");
		e->flags &= ~(EF_PENDING|EF_INFLIGHT);
	}
	else
		(void)strlcpy(runstate, "offline|", sizeof runstate);

	if (e->flags)
		errx(1, "%016" PRIx64 ": unexpected flags 0x%04x", e->id,
		    e->flags);

	if (status[0])
		status[strlen(status) - 1] = '\0';

	if (e->type == D_MDA)
		agent = "mda";
	else if (e->type == D_MTA)
		agent = "mta";
	else if (e->type == D_BOUNCE)
		agent = "bounce";

	if (e->ss.ss_family == AF_LOCAL)
		src = "local";
	else if (e->ss.ss_family == AF_INET)
		src = "inet4";
	else if (e->ss.ss_family == AF_INET6)
		src = "inet6";

	strnvis(errline, e->errorline, sizeof(errline), 0);

	printf("%016"PRIx64
	    "|%s|%s|%s|%s@%s|%s@%s|%s@%s"
	    "|%zu|%zu|%zu|%zu|%s|%s\n",

	    e->id,

	    src,
	    agent,
	    status,
	    e->sender.user, e->sender.domain,
	    e->rcpt.user, e->rcpt.domain,
	    e->dest.user, e->dest.domain,

	    (size_t) e->creation,
	    (size_t) (e->creation + e->ttl),
	    (size_t) e->lasttry,
	    (size_t) e->retry,
	    runstate,
	    errline);
}

static void
getflag(uint *bitmap, int bit, char *bitstr, char *buf, size_t len)
{
	if (*bitmap & bit) {
		*bitmap &= ~bit;
		(void)strlcat(buf, bitstr, len);
		(void)strlcat(buf, ",", len);
	}
}

static void
show_offline_envelope(uint64_t evpid)
{
	FILE   *fp = NULL;
	char	pathname[PATH_MAX];
	size_t	plen;
	char   *p;
	size_t	buflen;
	char	buffer[sizeof(struct envelope)];

	struct envelope	evp;

	if (!bsnprintf(pathname, sizeof pathname,
		"/queue/%02x/%08x/%016"PRIx64,
		(evpid_to_msgid(evpid) & 0xff000000) >> 24,
		evpid_to_msgid(evpid), evpid))
		goto end;
	fp = fopen(pathname, "r");
	if (fp == NULL)
		goto end;

	buflen = fread(buffer, 1, sizeof (buffer) - 1, fp);
	p = buffer;
	plen = buflen;
	buffer[buflen] = '\0';

	if (is_encrypted_buffer(p)) {
		warnx("offline encrypted queue is not supported yet");
		goto end;
	}

	if (is_gzip_buffer(p)) {
		warnx("offline compressed queue is not supported yet");
		goto end;
	}

	if (!envelope_load_buffer(&evp, p, plen))
		goto end;
	evp.id = evpid;
	show_queue_envelope(&evp, 0);

end:
	if (fp)
		fclose(fp);
}

static void
display(const char *s)
{
	FILE   *fp;
	char   *key;
	int	gzipped;
	char   *gzcat_argv0 = strrchr(PATH_GZCAT, '/') + 1;

	if ((fp = fopen(s, "r")) == NULL)
		err(1, "fopen");

	if (is_encrypted_fp(fp)) {
		int	i;
		FILE   *ofp = NULL;

		if ((ofp = tmpfile()) == NULL)
			err(1, "tmpfile");

		for (i = 0; i < 3; i++) {
			key = getpass("key> ");
			if (crypto_setup(key, strlen(key)))
				break;
		}
		if (i == 3)
			errx(1, "crypto-setup: invalid key");

		if (!crypto_decrypt_file(fp, ofp)) {
			printf("object is encrypted: %s\n", key);
			exit(1);
		}

		fclose(fp);
		fp = ofp;
		fseek(fp, 0, SEEK_SET);
	}
	gzipped = is_gzip_fp(fp);

	lseek(fileno(fp), 0, SEEK_SET);
	(void)dup2(fileno(fp), STDIN_FILENO);
	if (gzipped)
		execl(PATH_GZCAT, gzcat_argv0, (char *)NULL);
	else
		execl(PATH_CAT, "cat", (char *)NULL);
	err(1, "execl");
}

static int
str_to_trace(const char *str)
{
	if (!strcmp(str, "imsg"))
		return TRACE_IMSG;
	if (!strcmp(str, "io"))
		return TRACE_IO;
	if (!strcmp(str, "smtp"))
		return TRACE_SMTP;
	if (!strcmp(str, "filters"))
		return TRACE_FILTERS;
	if (!strcmp(str, "mta"))
		return TRACE_MTA;
	if (!strcmp(str, "bounce"))
		return TRACE_BOUNCE;
	if (!strcmp(str, "scheduler"))
		return TRACE_SCHEDULER;
	if (!strcmp(str, "lookup"))
		return TRACE_LOOKUP;
	if (!strcmp(str, "stat"))
		return TRACE_STAT;
	if (!strcmp(str, "rules"))
		return TRACE_RULES;
	if (!strcmp(str, "mproc"))
		return TRACE_MPROC;
	if (!strcmp(str, "expand"))
		return TRACE_EXPAND;
	if (!strcmp(str, "all"))
		return ~TRACE_DEBUG;
	errx(1, "invalid trace keyword: %s", str);
	return (0);
}

static int
str_to_profile(const char *str)
{
	if (!strcmp(str, "imsg"))
		return PROFILE_IMSG;
	if (!strcmp(str, "queue"))
		return PROFILE_QUEUE;
	errx(1, "invalid profile keyword: %s", str);
	return (0);
}

static int
is_gzip_buffer(const char *buffer)
{
	uint16_t	magic;

	memcpy(&magic, buffer, sizeof magic);
#define	GZIP_MAGIC	0x8b1f
	return (magic == GZIP_MAGIC);
}

static int
is_gzip_fp(FILE *fp)
{
	uint8_t		magic[2];
	int		ret = 0;

	if (fread(&magic, 1, sizeof magic, fp) != sizeof magic)
		goto end;

	ret = is_gzip_buffer((const char *)&magic);
end:
	fseek(fp, 0, SEEK_SET);
	return ret;
}


/* XXX */
/*
 * queue supports transparent encryption.
 * encrypted chunks are prefixed with an API version byte
 * which we ensure is unambiguous with gzipped / plain
 * objects.
 */

static int
is_encrypted_buffer(const char *buffer)
{
	uint8_t	magic;

	magic = *buffer;
#define	ENCRYPTION_MAGIC	0x1
	return (magic == ENCRYPTION_MAGIC);
}

static int
is_encrypted_fp(FILE *fp)
{
	uint8_t	magic;
	int	ret = 0;

	if (fread(&magic, 1, sizeof magic, fp) != sizeof magic)
		goto end;

	ret = is_encrypted_buffer((const char *)&magic);
end:
	fseek(fp, 0, SEEK_SET);
	return ret;
}
