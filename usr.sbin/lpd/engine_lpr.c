/*	$OpenBSD: engine_lpr.c,v 1.2 2019/04/04 19:25:45 eric Exp $	*/

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

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <netgroup.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lpd.h"
#include "lp.h"

#include "log.h"
#include "proc.h"

struct lpr_recvfile {
	TAILQ_ENTRY(lpr_recvfile)	 entry;
	char				*dfname;
};

struct lpr_recvjob {
	TAILQ_ENTRY(lpr_recvjob)	 entry;
	struct lp_printer		 lp;
	uint32_t			 connid;
	char				*hostfrom;
	char				*cfname;
	int				 dfcount;
	size_t				 dfsize;
	TAILQ_HEAD(, lpr_recvfile)	 df;
};

static void lpr_allowedhost(uint32_t, const struct sockaddr *);
static void lpr_allowedhost_res(uint32_t, const char *, const char *);
static int lpr_mkstemp(void);
static void lpr_displayq(uint32_t, const char *, int, struct lp_jobfilter *);
static void lpr_displayq_res(uint32_t, int, const char *, const char *);
static void lpr_rmjob(uint32_t, const char *, const char *,
    struct lp_jobfilter *);
static void lpr_rmjob_res(uint32_t, int, const char *, const char *);
static void lpr_recvjob(uint32_t, const char*, const char *);
static void lpr_recvjob_res(uint32_t, int);
static void lpr_recvjob_cf(uint32_t, size_t, const char *);
static void lpr_recvjob_df(uint32_t, size_t, const char *);
static void lpr_recvjob_clear(uint32_t);
static void lpr_recvjob_commit(uint32_t);
static void lpr_recvjob_rollback(uint32_t);
static void lpr_recvjob_free(struct lpr_recvjob *);
static int matchaddr(const char *, const struct sockaddr *, int *);
static int cmpsockaddr(const struct sockaddr *, const struct sockaddr *);

static TAILQ_HEAD(, lpr_recvjob) recvjobs = TAILQ_HEAD_INITIALIZER(recvjobs);

void
lpr_dispatch_frontend(struct imsgproc *proc, struct imsg *imsg)
{
	struct sockaddr_storage ss;
	struct lp_jobfilter jf;
	struct lp_printer lp;
	const char *hostfrom, *prn, *filename, *agent;
	uint32_t connid;
	size_t size;
	int lng, i;

	connid = imsg->hdr.peerid;

	switch (imsg->hdr.type) {
	case IMSG_LPR_ALLOWEDHOST:
		m_get_sockaddr(proc, (struct sockaddr *)&ss);
		m_end(proc);
		lpr_allowedhost(connid, (struct sockaddr *)&ss);
		break;

	case IMSG_LPR_DISPLAYQ:
		memset(&jf, 0, sizeof(jf));
		m_get_int(proc, &lng);
		m_get_string(proc, &jf.hostfrom);
		m_get_string(proc, &prn);
		m_get_int(proc, &jf.njob);
		for (i = 0; i < jf.njob; i++)
			m_get_int(proc, &jf.jobs[i]);
		m_get_int(proc, &jf.nuser);
		for (i = 0; i < jf.nuser; i++)
			m_get_string(proc, &jf.users[i]);
		m_end(proc);
		lpr_displayq(connid, prn, lng, &jf);
		break;

	case IMSG_LPR_PRINTJOB:
		m_get_string(proc, &prn);
		m_end(proc);
		/* Make sure the printer exists. */
		if (lp_getprinter(&lp, prn) == -1)
			break;
		lpr_printjob(lp.lp_name);
		lp_clearprinter(&lp);
		break;

	case IMSG_LPR_RECVJOB:
		m_get_string(proc, &hostfrom);
		m_get_string(proc, &prn);
		m_end(proc);
		lpr_recvjob(connid, hostfrom, prn);
		break;

	case IMSG_LPR_RECVJOB_CLEAR:
		m_end(proc);
		lpr_recvjob_clear(connid);
		break;

	case IMSG_LPR_RECVJOB_CF:
		m_get_size(proc, &size);
		m_get_string(proc, &filename);
		m_end(proc);
		lpr_recvjob_cf(connid, size, filename);
		break;

	case IMSG_LPR_RECVJOB_DF:
		m_get_size(proc, &size);
		m_get_string(proc, &filename);
		m_end(proc);
		lpr_recvjob_df(connid, size, filename);
		break;

	case IMSG_LPR_RECVJOB_COMMIT:
		m_end(proc);
		lpr_recvjob_commit(connid);
		break;

	case IMSG_LPR_RECVJOB_ROLLBACK:
		m_end(proc);
		lpr_recvjob_rollback(connid);
		break;

	case IMSG_LPR_RMJOB:
		memset(&jf, 0, sizeof(jf));
		m_get_string(proc, &jf.hostfrom);
		m_get_string(proc, &prn);
		m_get_string(proc, &agent);
		m_get_int(proc, &jf.njob);
		for (i = 0; i < jf.njob; i++)
			m_get_int(proc, &jf.jobs[i]);
		m_get_int(proc, &jf.nuser);
		for (i = 0; i < jf.nuser; i++)
			m_get_string(proc, &jf.users[i]);
		m_end(proc);
		lpr_rmjob(connid, prn, agent, &jf);
		break;

	default:
		fatalx("%s: unexpected imsg %s", __func__,
		    log_fmt_imsgtype(imsg->hdr.type));
	}
}

void
lpr_shutdown()
{
	struct lpr_recvjob *j;

	/* Cleanup incoming jobs. */
	while ((j = TAILQ_FIRST(&recvjobs))) {
		lpr_recvjob_clear(j->connid);
		lpr_recvjob_free(j);
	}
}

void
lpr_printjob(const char *prn)
{
	m_create(p_priv, IMSG_LPR_PRINTJOB, 0, 0, -1);
	m_add_string(p_priv, prn);
	m_close(p_priv);
}

static void
lpr_allowedhost(uint32_t connid, const struct sockaddr *sa)
{
	FILE *fp;
	size_t linesz = 0;
	ssize_t linelen;
	char host[NI_MAXHOST], addr[NI_MAXHOST], serv[NI_MAXSERV];
	char dom[NI_MAXHOST], *lp, *ep, *line = NULL;
	int e, rev = 0, ok = 0;

	/* Always accept local connections. */
	if (sa->sa_family == AF_UNIX) {
		lpr_allowedhost_res(connid, lpd_hostname, NULL);
		return;
	}

	host[0] = '\0';

	/* Print address. */
	if ((e = getnameinfo(sa, sa->sa_len, addr, sizeof(addr), serv,
	    sizeof(serv), NI_NUMERICHOST))) {
		log_warnx("%s: could not print addr: %s", __func__,
		    gai_strerror(e));
		lpr_allowedhost_res(connid, host, "Malformed address");
		return;
	}

	/* Get the hostname for the address. */
	if ((e = getnameinfo(sa, sa->sa_len, host, sizeof(host), NULL, 0,
	    NI_NAMEREQD))) {
		if (e != EAI_NONAME)
			log_warnx("%s: could not resolve %s: %s", __func__,
			    addr, gai_strerror(e));
		lpr_allowedhost_res(connid, host,
		    "No hostname found for your address");
		return;
	}

	/* Check for a valid DNS roundtrip. */
	if (!matchaddr(host, sa, &e)) {
		if (e)
			log_warnx("%s: getaddrinfo: %s: %s", __func__,
			    host, gai_strerror(e));
		lpr_allowedhost_res(connid, host, e ?
		    "Cannot resolve your hostname" :
		    "Your hostname and your address do not match");
		return;
	}

	/* Scan the hosts.lpd file. */
	if ((fp = fopen(_PATH_HOSTSLPD, "r")) == NULL) {
		log_warn("%s: %s", __func__, _PATH_HOSTSLPD);
		lpr_allowedhost_res(connid, host,
		    "Cannot access " _PATH_HOSTSLPD);
		return;
	}

	dom[0] = '\0';
	while ((linelen = getline(&line, &linesz, fp)) != -1) {
		/* Drop comment and strip line. */
		for (lp = line; *lp; lp++)
			if (!isspace((unsigned char)*lp))
				break;
		if (*lp == '#' || *lp == '\0')
			continue;
		for (ep = lp + 1; *ep; ep++)
			if (isspace((unsigned char)*ep) || *ep == '#') {
				*ep = '\0';
				break;
			}

		rev = 0;
		switch (lp[0]) {
		case '-':
		case '+':
			switch (lp[1]) {
			case '\0':
				ok = 1;
				break;
			case '@':
				if (dom[0] == '\0')
					getdomainname(dom, sizeof(dom));
				ok = innetgr(lp + 2, host, NULL, dom);
				break;
			default:
				ok = matchaddr(lp + 1, sa, NULL);
				break;
			}
			if (lp[0] == '-')
				ok = -ok;
			break;
		default:
			ok = matchaddr(lp, sa, NULL);
			break;
		}
		if (ok)
			break;
	}

	free(line);
	fclose(fp);

	lpr_allowedhost_res(connid, host,
	    (ok > 0) ? NULL : "Access denied");
}

static void
lpr_allowedhost_res(uint32_t connid, const char *hostname, const char *reject)
{
	m_create(p_frontend, IMSG_LPR_ALLOWEDHOST, connid, 0, -1);
	m_add_string(p_frontend, hostname);
	m_add_string(p_frontend, reject);
	m_close(p_frontend);
}

static int
matchaddr(const char *host, const struct sockaddr *sa, int *gaierrno)
{
	struct addrinfo hints, *res, *r;
	int e, ok = 0;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = sa->sa_family;
	hints.ai_socktype = SOCK_DGRAM; /*dummy*/
	if ((e = getaddrinfo(host, NULL, &hints, &res))) {
		if (gaierrno)
			*gaierrno = e;
		return 0;
	}
	if (gaierrno)
		*gaierrno = 0;

	for (r = res; r; r = r->ai_next)
		if (cmpsockaddr(sa, r->ai_addr) == 0) {
			ok = 1;
			break;
		}
	freeaddrinfo(res);

	return ok;
}

static int
cmpsockaddr(const struct sockaddr *a, const struct sockaddr *b)
{
	const void *aa, *ab;
	size_t l;

	if (a->sa_family != b->sa_family)
		return (a->sa_family < b->sa_family) ? -1 : 1;

	switch (a->sa_family) {
	case AF_UNIX:
		return 0;

	case AF_INET:
		aa = &(((const struct sockaddr_in*)a)->sin_addr);
		ab = &(((const struct sockaddr_in*)b)->sin_addr);
		l = sizeof(((const struct sockaddr_in*)a)->sin_addr);
		return memcmp(aa, ab, l);

	case AF_INET6:
		aa = &(((const struct sockaddr_in6*)a)->sin6_addr);
		ab = &(((const struct sockaddr_in6*)b)->sin6_addr);
		l = sizeof(((const struct sockaddr_in*)a)->sin_addr);
		return memcmp(aa, ab, l);

	}

	return 0;
}

static int
lpr_mkstemp(void)
{
	char path[PATH_MAX];
	int fd;

	if (strlcpy(path, _PATH_TMP "lpd.XXXXXXXXXX", sizeof(path)) >=
	    sizeof(path)) {
		log_warnx("%s: path too long", __func__);
		return -1;
	}
	if ((fd = mkstemp(path)) == -1) {
		log_warn("%s: mkstemp", __func__);
		return -1;
	}
	(void)unlink(path);
	return fd;

}

static void
lpr_displayq(uint32_t connid, const char *prn, int lng, struct lp_jobfilter *jf)
{
	struct lp_printer lp;
	char cmd[LPR_MAXCMDLEN], buf[16];
	int fd, i;

	if (lp_getprinter(&lp, prn) == -1) {
		lpr_displayq_res(connid, -1, NULL, NULL);
		return;
	}

	fd = lpr_mkstemp();
	if (fd != -1) {
		/* Write formatted queue content into the temporary file. */
		lp_displayq(fd, &lp, lng, jf);
		if (lseek(fd, 0, SEEK_SET) == -1)
			log_warn("%s: lseek", __func__);
	}

	/* Send the result to frontend. */
	if (lp.lp_type == PRN_LPR) {
		snprintf(cmd, sizeof(cmd), "%c%s", lng?'\4':'\3', LP_RP(&lp));
		for (i = 0; i < jf->nuser; i++) {
			strlcat(cmd, " ", sizeof(cmd));
			strlcat(cmd, jf->users[i], sizeof(cmd));
		}
		for (i = 0; i < jf->njob; i++) {
			snprintf(buf, sizeof(buf), " %d", jf->jobs[i]);
			strlcat(cmd, buf, sizeof(cmd));
		}
		lpr_displayq_res(connid, fd, lp.lp_host, cmd);
	}
	else
		lpr_displayq_res(connid, fd, NULL, NULL);

	lp_clearprinter(&lp);
}

static void
lpr_displayq_res(uint32_t connid, int fd, const char *host, const char *cmd)
{
	m_create(p_frontend, IMSG_LPR_DISPLAYQ, connid, 0, fd);
	m_add_string(p_frontend, host);
	m_add_string(p_frontend, cmd);
	m_close(p_frontend);
}

static void
lpr_rmjob(uint32_t connid, const char *prn, const char *agent,
    struct lp_jobfilter *jf)
{
	struct lp_printer lp;
	char cmd[LPR_MAXCMDLEN], buf[16];
	int fd, i, restart = 0;

	if (lp_getprinter(&lp, prn) == -1) {
		lpr_rmjob_res(connid, -1, NULL, NULL);
		return;
	}

	fd = lpr_mkstemp();
	if (fd != -1) {
		/* Write result to the temporary file. */
		restart = lp_rmjob(fd, &lp, agent, jf);
		if (lseek(fd, 0, SEEK_SET) == -1)
			log_warn("%s: lseek", __func__);
	}

	/* Send the result to frontend. */
	if (lp.lp_type == PRN_LPR) {
		snprintf(cmd, sizeof(cmd), "\5%s %s", LP_RP(&lp), agent);
		for (i = 0; i < jf->nuser; i++) {
			strlcat(cmd, " ", sizeof(cmd));
			strlcat(cmd, jf->users[i], sizeof(cmd));
		}
		for (i = 0; i < jf->njob; i++) {
			snprintf(buf, sizeof(buf), " %d", jf->jobs[i]);
			strlcat(cmd, buf, sizeof(cmd));
		}
		lpr_rmjob_res(connid, fd, lp.lp_host, cmd);
	}
	else
		lpr_rmjob_res(connid, fd, NULL, NULL);

	/* If the printer process was stopped, tell parent to re-spawn one. */
	if (restart)
		lpr_printjob(lp.lp_name);

	lp_clearprinter(&lp);
}

static void
lpr_rmjob_res(uint32_t connid, int fd, const char *host, const char *cmd)
{
	m_create(p_frontend, IMSG_LPR_RMJOB, connid, 0, fd);
	m_add_string(p_frontend, host);
	m_add_string(p_frontend, cmd);
	m_close(p_frontend);
}

static void
lpr_recvjob(uint32_t connid, const char *hostfrom, const char *prn)
{
	struct lpr_recvjob *j;
	int qstate;

	if ((j = calloc(1, sizeof(*j))) == NULL) {
		log_warn("%s: calloc", __func__);
		goto fail;
	}
	if (lp_getprinter(&j->lp, prn) == -1)
		goto fail;

	/* Make sure queueing is not disabled. */
	if (lp_getqueuestate(&j->lp, 0, &qstate) == -1) {
		log_warnx("cannot get queue state");
		goto fail;
	}
	if (qstate & LPQ_QUEUE_OFF)
		goto fail;

	if ((j->hostfrom = strdup(hostfrom)) == NULL) {
		log_warn("%s: strdup", __func__);
		goto fail;
	}

	j->connid = connid;
	TAILQ_INIT(&j->df);
	TAILQ_INSERT_TAIL(&recvjobs, j, entry);

	lpr_recvjob_res(connid, LPR_ACK);
	return;

    fail:
	if (j) {
		lp_clearprinter(&j->lp);
		free(j->hostfrom);
	}
	free(j);
	lpr_recvjob_res(connid, LPR_NACK);
}

static void
lpr_recvjob_res(uint32_t connid, int ack)
{
	m_create(p_frontend, IMSG_LPR_RECVJOB, connid, 0, -1);
	m_add_int(p_frontend, ack);
	m_close(p_frontend);
}

static void
lpr_recvjob_cf(uint32_t connid, size_t size, const char *filename)
{
	struct lpr_recvjob *j;
	char fname[PATH_MAX];
	int fd;

	fd = -1;
	TAILQ_FOREACH(j, &recvjobs, entry)
		if (j->connid == connid)
			break;
	if (j == NULL) {
		log_warnx("invalid job id");
		goto done;
	}

	if (j->cfname) {
		log_warnx("duplicate control file");
		goto done;
	}

	if (!lp_validfilename(filename, 1)) {
		log_warnx("invalid control file name %s", filename);
		goto done;
	}

	/* Rewrite file to make sure the hostname is correct. */
	(void)strlcpy(fname, filename, 7);
	if (strlcat(fname, j->hostfrom, sizeof(fname)) >= sizeof(fname)) {
		log_warnx("filename too long");
		goto done;
	}

	if ((j->cfname = strdup(fname)) == NULL) {
		log_warn("%s: stdrup", __func__);
		goto done;
	}

	fd = lp_create(&j->lp, 1, size, j->cfname);
	if (fd == -1) {
		if (errno == EFBIG || errno == ENOSPC)
			log_warn("rejected control file");
		else
			log_warnx("cannot create control file");
		free(j->cfname);
		j->cfname = NULL;
	}

    done:
	m_create(p_frontend, IMSG_LPR_RECVJOB_CF, connid, 0, fd);
	m_add_int(p_frontend, (fd == -1) ? LPR_NACK : LPR_ACK);
	m_add_size(p_frontend, size);
	m_close(p_frontend);
}

static void
lpr_recvjob_df(uint32_t connid, size_t size, const char *filename)
{
	struct lpr_recvfile *f;
	struct lpr_recvjob *j;
	int fd;

	fd = -1;
	TAILQ_FOREACH(j, &recvjobs, entry)
		if (j->connid == connid)
			break;
	if (j == NULL) {
		log_warnx("invalid job id");
		goto done;
	}

	if (!lp_validfilename(filename, 0)) {
		log_warnx("invalid data file name %s", filename);
		goto done;
	}

	if ((f = calloc(1, sizeof(*f))) == NULL) {
		log_warn("%s: calloc", __func__);
		goto done;
	}

	if ((f->dfname = strdup(filename)) == NULL) {
		log_warn("%s: strdup", __func__);
		free(f);
		goto done;
	}

	fd = lp_create(&j->lp, 0, size, f->dfname);
	if (fd == -1) {
		if (errno == EFBIG || errno == ENOSPC)
			log_warn("rejected data file");
		else
			log_warnx("cannot create data file");
		free(f->dfname);
		free(f);
		goto done;
	}

	j->dfcount += 1;
	j->dfsize += size;
	TAILQ_INSERT_TAIL(&j->df, f, entry);

    done:
	m_create(p_frontend, IMSG_LPR_RECVJOB_DF, connid, 0, fd);
	m_add_int(p_frontend, (fd == -1) ? LPR_NACK : LPR_ACK);
	m_add_size(p_frontend, size);
	m_close(p_frontend);
}

static void
lpr_recvjob_clear(uint32_t connid)
{
	struct lpr_recvfile *f;
	struct lpr_recvjob *j;

	TAILQ_FOREACH(j, &recvjobs, entry)
		if (j->connid == connid)
			break;
	if (j == NULL) {
		log_warnx("invalid job id");
		return;
	}

	if (j->cfname) {
		j->cfname[0] = 'c';
		if (lp_unlink(&j->lp, j->cfname) == -1)
			log_warn("cannot unlink %s", j->cfname);
		j->cfname[0] = 't';
		if (lp_unlink(&j->lp, j->cfname) == 1)
			log_warn("cannot unlink %s", j->cfname);
		free(j->cfname);
		j->cfname = NULL;
	}

	while ((f = TAILQ_FIRST(&j->df))) {
		TAILQ_REMOVE(&j->df, f, entry);
		if (lp_unlink(&j->lp, f->dfname) == -1)
			log_warn("cannot unlink %s", f->dfname);
		free(f->dfname);
		free(f);
	}
}

static void
lpr_recvjob_commit(uint32_t connid)
{
	struct lpr_recvjob *j;
	int ack;

	ack = LPR_NACK;
	TAILQ_FOREACH(j, &recvjobs, entry)
		if (j->connid == connid)
			break;
	if (j == NULL) {
		log_warnx("invalid job id");
		return;
	}

	if (!j->cfname) {
		log_warnx("no control file received from %s", j->hostfrom);
		lpr_recvjob_clear(connid);
		lpr_recvjob_free(j);
		return;
	}

	if ((lp_commit(&j->lp, j->cfname) == -1)) {
		log_warn("cannot commit %s", j->cfname);
		lpr_recvjob_clear(connid);
		lpr_recvjob_free(j);
		return;
	}

	log_info("received job %s printer=%s host=%s files=%d size=%zu",
	    j->cfname, j->lp.lp_name, j->hostfrom, j->dfcount, j->dfsize);

	/* Start the printer. */
	lpr_printjob(j->lp.lp_name);
	lpr_recvjob_free(j);
}

static void
lpr_recvjob_rollback(uint32_t connid)
{
	struct lpr_recvjob *j;

	lpr_recvjob_clear(connid);

	TAILQ_FOREACH(j, &recvjobs, entry)
		if (j->connid == connid)
			break;
	if (j == NULL) {
		log_warnx("invalid job id");
		return;
	}
	lpr_recvjob_free(j);
}

static void
lpr_recvjob_free(struct lpr_recvjob *j)
{
	struct lpr_recvfile *f;

	TAILQ_REMOVE(&recvjobs, j, entry);
	lp_clearprinter(&j->lp);
	free(j->hostfrom);
	free(j->cfname);
	while ((f = TAILQ_FIRST(&j->df))) {
		TAILQ_REMOVE(&j->df, f, entry);
		free(f->dfname);
		free(f);
	}
}
