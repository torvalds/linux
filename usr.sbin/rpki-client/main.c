/*	$OpenBSD: main.c,v 1.298 2025/09/09 08:23:24 job Exp $ */
/*
 * Copyright (c) 2021 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2019 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <sys/queue.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/tree.h>
#include <sys/wait.h>

#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <imsg.h>

#include "extern.h"
#include "version.h"

const char	*tals[TALSZ_MAX];
const char	*taldescs[TALSZ_MAX];
unsigned int	 talrepocnt[TALSZ_MAX];
struct repotalstats talstats[TALSZ_MAX];
int		 talsz;

size_t	entity_queue;
int	timeout = 60*60;
volatile sig_atomic_t killme;
volatile sig_atomic_t printinfo;
void	sighandler(int sig);

static struct filepath_tree	fpt = RB_INITIALIZER(&fpt);
static struct msgbuf		*procq, *rsyncq, *httpq, *rrdpq;
static int			cachefd, outdirfd;

int	verbose;
int	noop;
int	excludeas0 = 1;
int	excludeaspa;
int	filemode;
int	shortlistmode;
int	rrdpon = 1;
int	repo_timeout;
int	experimental;
time_t	deadline;

/* 9999-12-31 23:59:59 UTC */
#define X509_TIME_MAX 253402300799LL
/* 0000-01-01 00:00:00 UTC */
#define X509_TIME_MIN -62167219200LL

int64_t  evaluation_time = X509_TIME_MIN;

struct stats	 stats;

struct fqdnlistentry {
	LIST_ENTRY(fqdnlistentry)	 entry;
	char				*fqdn;
};
LIST_HEAD(fqdns, fqdnlistentry);

struct fqdns shortlist = LIST_HEAD_INITIALIZER(fqdns);
struct fqdns skiplist = LIST_HEAD_INITIALIZER(fqdns);

/*
 * Log a message to stderr if and only if "verbose" is non-zero.
 * This uses the err(3) functionality.
 */
void
logx(const char *fmt, ...)
{
	va_list		 ap;

	if (verbose && fmt != NULL) {
		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
	}
}

time_t
getmonotime(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		err(1, "clock_gettime");
	return (ts.tv_sec);
}

/*
 * Time - Evaluation time is used as the current time if it is
 * larger than X509_TIME_MIN, otherwise the system time is used.
 */
time_t
get_current_time(void)
{
	if (evaluation_time > X509_TIME_MIN)
		return (time_t)evaluation_time;
	return time(NULL);
}

void
entity_free(struct entity *ent)
{
	if (ent == NULL)
		return;

	free(ent->path);
	free(ent->file);
	free(ent->mftaki);
	free(ent->data);
	free(ent);
}

/*
 * Read a queue entity from the descriptor.
 * Matched by entity_write_req() and entity_write_repo().
 * Both the parser and filemode modules use this function.
 * The pointer must be passed entity_free().
 */
void
entity_read_req(struct ibuf *b, struct entity *ent)
{
	io_read_buf(b, &ent->type, sizeof(ent->type));
	io_read_buf(b, &ent->location, sizeof(ent->location));
	io_read_buf(b, &ent->repoid, sizeof(ent->repoid));

	if (ent->type == RTYPE_REPO) {
		io_read_opt_str(b, &ent->path);
		io_read_str(b, &ent->file);
	} else {
		io_read_buf(b, &ent->talid, sizeof(ent->talid));
		io_read_buf(b, &ent->certid, sizeof(ent->certid));
		io_read_opt_str(b, &ent->path);
		io_read_str(b, &ent->file);
		io_read_opt_str(b, &ent->mftaki);
		io_read_buf_alloc(b, (void **)&ent->data, &ent->datasz);
	}
}

/*
 * Write the queue entity.
 * Matched by entity_read_req() above.
 */
static void
entity_write_req(const struct entity *ent)
{
	struct ibuf *b;

	b = io_new_buffer();
	io_simple_buffer(b, &ent->type, sizeof(ent->type));
	io_simple_buffer(b, &ent->location, sizeof(ent->location));
	io_simple_buffer(b, &ent->repoid, sizeof(ent->repoid));
	io_simple_buffer(b, &ent->talid, sizeof(ent->talid));
	io_simple_buffer(b, &ent->certid, sizeof(ent->certid));
	io_opt_str_buffer(b, ent->path);
	io_str_buffer(b, ent->file);
	io_opt_str_buffer(b, ent->mftaki);
	io_buf_buffer(b, ent->data, ent->datasz);
	io_close_buffer(procq, b);
}

/*
 * Write a repository entity to inform the parser of a new repository.
 * Matched by entity_read_req() above.
 */
static void
entity_write_repo(const struct repo *rp)
{
	struct ibuf *b;
	enum rtype type = RTYPE_REPO;
	enum location loc = DIR_UNKNOWN;
	unsigned int repoid;
	char *path, *altpath;

	repoid = repo_id(rp);
	path = repo_basedir(rp, 0);
	altpath = repo_basedir(rp, 1);

	b = io_new_buffer();
	io_simple_buffer(b, &type, sizeof(type));
	io_simple_buffer(b, &loc, sizeof(loc));
	io_simple_buffer(b, &repoid, sizeof(repoid));
	io_opt_str_buffer(b, path);
	io_str_buffer(b, altpath);
	io_close_buffer(procq, b);
	free(path);
	free(altpath);
}

/*
 * Scan through all queued requests and see which ones are in the given
 * repo, then flush those into the parser process.
 */
void
entityq_flush(struct entityq *q, struct repo *rp)
{
	struct entity	*p, *np;

	entity_write_repo(rp);

	TAILQ_FOREACH_SAFE(p, q, entries, np) {
		entity_write_req(p);
		TAILQ_REMOVE(q, p, entries);
		entity_free(p);
	}
}

/*
 * Add the heap-allocated file to the queue for processing.
 */
static void
entityq_add(char *path, char *file, enum rtype type, enum location loc,
    struct repo *rp, unsigned char *data, size_t datasz, int talid, int certid,
    char *mftaki)
{
	struct entity	*p;

	if ((p = calloc(1, sizeof(struct entity))) == NULL)
		err(1, NULL);

	p->type = type;
	p->location = loc;
	p->talid = talid;
	p->certid = certid;
	p->mftaki = mftaki;
	p->path = path;
	if (rp != NULL)
		p->repoid = repo_id(rp);
	p->file = file;
	p->data = data;
	p->datasz = (data != NULL) ? datasz : 0;

	entity_queue++;

	/*
	 * Write to the queue if there's no repo or the repo has already
	 * been loaded else enqueue it for later.
	 */

	if (rp == NULL || !repo_queued(rp, p)) {
		entity_write_req(p);
		entity_free(p);
	}
}

static void
rrdp_file_resp(unsigned int id, int ok)
{
	enum rrdp_msg type = RRDP_FILE;
	struct ibuf *b;

	b = io_new_buffer();
	io_simple_buffer(b, &type, sizeof(type));
	io_simple_buffer(b, &id, sizeof(id));
	io_simple_buffer(b, &ok, sizeof(ok));
	io_close_buffer(rrdpq, b);
}

void
rrdp_fetch(unsigned int id, const char *uri, const char *local,
    struct rrdp_session *s)
{
	enum rrdp_msg type = RRDP_START;
	struct ibuf *b;
	int c;

	b = io_new_buffer();
	io_simple_buffer(b, &type, sizeof(type));
	io_simple_buffer(b, &id, sizeof(id));
	io_str_buffer(b, local);
	io_str_buffer(b, uri);
	c = (s->session_id != NULL);
	io_simple_buffer(b, &c, sizeof(c));

	if (c != 0)
		rrdp_session_buffer(b, s);
	io_close_buffer(rrdpq, b);
}

void
rrdp_abort(unsigned int id)
{
	enum rrdp_msg type = RRDP_ABORT;
	struct ibuf *b;

	b = io_new_buffer();
	io_simple_buffer(b, &type, sizeof(type));
	io_simple_buffer(b, &id, sizeof(id));
	io_close_buffer(rrdpq, b);
}

/*
 * Request a repository sync via rsync URI to directory local.
 */
void
rsync_fetch(unsigned int id, const char *uri, const char *local,
    const char *base)
{
	struct ibuf	*b;

	b = io_new_buffer();
	io_simple_buffer(b, &id, sizeof(id));
	io_str_buffer(b, local);
	io_opt_str_buffer(b, base);
	io_str_buffer(b, uri);
	io_close_buffer(rsyncq, b);
}

void
rsync_abort(unsigned int id)
{
	struct ibuf	*b;

	b = io_new_buffer();
	io_simple_buffer(b, &id, sizeof(id));
	io_opt_str_buffer(b, NULL);
	io_opt_str_buffer(b, NULL);
	io_opt_str_buffer(b, NULL);
	io_close_buffer(rsyncq, b);
}

/*
 * Request a file from a https uri, data is written to the file descriptor fd.
 */
void
http_fetch(unsigned int id, const char *uri, const char *last_mod, int fd)
{
	struct ibuf	*b;

	b = io_new_buffer();
	io_simple_buffer(b, &id, sizeof(id));
	io_str_buffer(b, uri);
	io_opt_str_buffer(b, last_mod);
	/* pass file as fd */
	ibuf_fd_set(b, fd);
	io_close_buffer(httpq, b);
}

/*
 * Request some XML file on behalf of the rrdp parser.
 * Create a pipe and pass the pipe endpoints to the http and rrdp process.
 */
static void
rrdp_http_fetch(unsigned int id, const char *uri, const char *last_mod)
{
	enum rrdp_msg type = RRDP_HTTP_INI;
	struct ibuf *b;
	int pi[2];

	if (pipe2(pi, O_CLOEXEC | O_NONBLOCK) == -1)
		err(1, "pipe");

	b = io_new_buffer();
	io_simple_buffer(b, &type, sizeof(type));
	io_simple_buffer(b, &id, sizeof(id));
	ibuf_fd_set(b, pi[0]);
	io_close_buffer(rrdpq, b);

	http_fetch(id, uri, last_mod, pi[1]);
}

void
rrdp_http_done(unsigned int id, enum http_result res, const char *last_mod)
{
	enum rrdp_msg type = RRDP_HTTP_FIN;
	struct ibuf *b;

	/* RRDP request, relay response over to the rrdp process */
	b = io_new_buffer();
	io_simple_buffer(b, &type, sizeof(type));
	io_simple_buffer(b, &id, sizeof(id));
	io_simple_buffer(b, &res, sizeof(res));
	io_opt_str_buffer(b, last_mod);
	io_close_buffer(rrdpq, b);
}

/*
 * Add a file (CER, ROA, CRL, ...) from a manifest fileList, RFC 9286.
 * These are always relative to the directory in which "mft" sits.
 */
static void
queue_add_from_mft(const struct mft *mft)
{
	size_t			 i;
	struct repo		*rp;
	const struct mftfile	*f;
	char			*mftaki, *nfile, *npath = NULL;

	rp = repo_byid(mft->repoid);
	for (i = 0; i < mft->filesz; i++) {
		f = &mft->files[i];

		if (f->type == RTYPE_INVALID || f->type == RTYPE_CRL)
			continue;

		if (mft->path != NULL)
			if ((npath = strdup(mft->path)) == NULL)
				err(1, NULL);
		if ((nfile = strdup(f->file)) == NULL)
			err(1, NULL);
		if ((mftaki = strdup(mft->aki)) == NULL)
			err(1, NULL);
		entityq_add(npath, nfile, f->type, f->location, rp, NULL, 0,
		    mft->talid, mft->certid, mftaki);
	}
}

/*
 * Add a local file to the queue of files to fetch.
 */
static void
queue_add_file(const char *file, enum rtype type, int talid)
{
	unsigned char	*buf = NULL;
	char		*nfile;
	size_t		 len = 0;

	if (!filemode || strncmp(file, RSYNC_PROTO, RSYNC_PROTO_LEN) != 0) {
		buf = load_file(file, &len);
		if (buf == NULL)
			err(1, "%s", file);
	}

	if ((nfile = strdup(file)) == NULL)
		err(1, NULL);
	/* Not in a repository, so directly add to queue. */
	entityq_add(NULL, nfile, type, DIR_UNKNOWN, NULL, buf, len, talid, 0,
	    NULL);
}

/*
 * Add URIs (CER) from a TAL file, RFC 8630.
 */
static void
queue_add_from_tal(struct tal *tal)
{
	struct repo	*repo;
	unsigned char	*data;
	char		*nfile;

	assert(tal->num_uris > 0);

	if ((taldescs[tal->id] = strdup(tal->descr)) == NULL)
		err(1, NULL);

	/* figure out the TA filename, must be done before repo lookup */
	nfile = strrchr(tal->uri[0], '/');
	assert(nfile != NULL);
	if ((nfile = strdup(nfile + 1)) == NULL)
		err(1, NULL);

	/* Look up the repository. */
	repo = ta_lookup(tal->id, tal);
	if (repo == NULL) {
		free(nfile);
		return;
	}

	/* steal the pkey from the tal structure */
	data = tal->pkey;
	tal->pkey = NULL;
	entityq_add(NULL, nfile, RTYPE_CER, DIR_UNKNOWN, repo, data,
	    tal->pkeysz, tal->id, tal->id, NULL);
}

/*
 * Add a manifest (MFT) found in an X509 certificate, RFC 6487.
 */
static void
queue_add_from_cert(const struct cert *cert, struct nca_tree *ncas)
{
	struct repo		*repo;
	struct fqdnlistentry	*le;
	char			*nfile, *npath, *host;
	const char		*uri, *repouri, *file;
	size_t			 repourisz;
	int			 shortlisted = 0;

	if (strncmp(cert->repo, RSYNC_PROTO, RSYNC_PROTO_LEN) != 0)
		errx(1, "unexpected protocol");
	host = cert->repo + RSYNC_PROTO_LEN;

	LIST_FOREACH(le, &skiplist, entry) {
		if (strncasecmp(host, le->fqdn, strcspn(host, "/")) == 0) {
			warnx("skipping %s (listed in skiplist)", cert->repo);
			return;
		}
	}

	LIST_FOREACH(le, &shortlist, entry) {
		if (strncasecmp(host, le->fqdn, strcspn(host, "/")) == 0) {
			shortlisted = 1;
			break;
		}
	}
	if (shortlistmode && shortlisted == 0) {
		if (verbose)
			warnx("skipping %s (not shortlisted)", cert->repo);
		return;
	}

	repo = repo_lookup(cert->talid, cert->repo,
	    rrdpon ? cert->notify : NULL);
	if (repo == NULL)
		return;

	/*
	 * Figure out the cert filename and path by chopping up the
	 * MFT URI in the cert based on the repo base URI.
	 */
	uri = cert->mft;
	repouri = repo_uri(repo);
	repourisz = strlen(repouri);
	if (strncmp(repouri, cert->mft, repourisz) != 0) {
		warnx("%s: URI %s outside of repository", repouri, uri);
		return;
	}
	uri += repourisz + 1;	/* skip base and '/' */
	file = strrchr(uri, '/');
	if (file == NULL) {
		npath = NULL;
		if ((nfile = strdup(uri)) == NULL)
			err(1, NULL);
	} else {
		if ((npath = strndup(uri, file - uri)) == NULL)
			err(1, NULL);
		if ((nfile = strdup(file + 1)) == NULL)
			err(1, NULL);
	}

	cert_insert_nca(ncas, cert, repo);
	entityq_add(npath, nfile, RTYPE_MFT, DIR_UNKNOWN, repo, NULL, 0,
	    cert->talid, cert->certid, NULL);
}

/*
 * Process parsed content.
 * For non-ROAs, we grok for more data.
 * For ROAs, we want to extract the valid info.
 * In all cases, we gather statistics.
 */
static void
entity_process(struct ibuf *b, struct validation_data *vd, struct stats *st)
{
	enum rtype	 type;
	struct tal	*tal;
	struct cert	*cert;
	struct mft	*mft;
	struct roa	*roa;
	struct aspa	*aspa;
	struct spl	*spl;
	struct repo	*rp = NULL;
	char		*file;
	time_t		 mtime;
	unsigned int	 id;
	int		 talid;
	int		 ok = 1;

	/*
	 * For most of these, we first read whether there's any content
	 * at all---this means that the syntactic parse failed (X509
	 * certificate, for example).
	 * We follow that up with whether the resources didn't parse.
	 */
	io_read_buf(b, &type, sizeof(type));
	io_read_buf(b, &id, sizeof(id));
	io_read_buf(b, &talid, sizeof(talid));
	io_read_str(b, &file);
	io_read_buf(b, &mtime, sizeof(mtime));

	/* in filemode messages can be ignored, only the accounting matters */
	if (filemode)
		goto done;

	if (file == NULL)
		errx(1, "no filename present in entity response");
	if (filepath_valid(&fpt, file, talid)) {
		warnx("%s: File already visited", file);
		goto done;
	}

	if (type != RTYPE_TAL) {
		rp = repo_byid(id);
		repo_stat_inc(rp, talid, type, STYPE_OK);
		repostats_new_files_inc(rp, file);
	}

	switch (type) {
	case RTYPE_TAL:
		st->tals++;
		tal = tal_read(b);
		queue_add_from_tal(tal);
		tal_free(tal);
		break;
	case RTYPE_CER:
		io_read_buf(b, &ok, sizeof(ok));
		if (ok == 0) {
			repo_stat_inc(rp, talid, type, STYPE_FAIL);
			break;
		}
		cert = cert_read(b);
		switch (cert->purpose) {
		case CERT_PURPOSE_TA:
			ccr_insert_tas(&vd->ccr.tas, cert);
			/* FALLTHROUGH */
		case CERT_PURPOSE_CA:
			queue_add_from_cert(cert, &vd->ncas);
			break;
		case CERT_PURPOSE_BGPSEC_ROUTER:
			cert_insert_brks(&vd->brks, cert);
			repo_stat_inc(rp, talid, type, STYPE_BGPSEC);
			break;
		default:
			errx(1, "unexpected %s", purpose2str(cert->purpose));
			break;
		}
		cert_free(cert);
		break;
	case RTYPE_MFT:
		io_read_buf(b, &ok, sizeof(ok));
		if (ok == 0) {
			repo_stat_inc(rp, talid, type, STYPE_FAIL);
			break;
		}
		mft = mft_read(b);
		if (mft->seqnum_gap)
			repo_stat_inc(rp, talid, type, STYPE_SEQNUM_GAP);
		queue_add_from_mft(mft);
		cert_remove_nca(&vd->ncas, mft->certid, rp);
		ccr_insert_mft(&vd->ccr.mfts, mft);
		mft_free(mft);
		break;
	case RTYPE_CRL:
		/* CRLs are sent together with MFT and not accounted for */
		entity_queue++;
		break;
	case RTYPE_ROA:
		io_read_buf(b, &ok, sizeof(ok));
		if (ok == 0) {
			repo_stat_inc(rp, talid, type, STYPE_FAIL);
			break;
		}
		roa = roa_read(b);
		if (roa->valid) {
			roa_insert_vrps(&vd->vrps, roa, rp);
			ccr_insert_roa(&vd->ccr.vrps, roa);
		} else
			repo_stat_inc(rp, talid, type, STYPE_INVALID);
		roa_free(roa);
		break;
	case RTYPE_GBR:
		break;
	case RTYPE_ASPA:
		io_read_buf(b, &ok, sizeof(ok));
		if (ok == 0) {
			repo_stat_inc(rp, talid, type, STYPE_FAIL);
			break;
		}
		aspa = aspa_read(b);
		if (aspa->valid)
			aspa_insert_vaps(file, &vd->vaps, aspa, rp);
		else
			repo_stat_inc(rp, talid, type, STYPE_INVALID);
		aspa_free(aspa);
		break;
	case RTYPE_SPL:
		io_read_buf(b, &ok, sizeof(ok));
		if (ok == 0) {
			if (experimental)
				repo_stat_inc(rp, talid, type, STYPE_FAIL);
			break;
		}
		spl = spl_read(b);
		if (spl->valid)
			spl_insert_vsps(&vd->vsps, spl, rp);
		else
			repo_stat_inc(rp, talid, type, STYPE_INVALID);
		spl_free(spl);
		break;
	case RTYPE_TAK:
		break;
	case RTYPE_FILE:
		break;
	default:
		warnx("%s: unknown entity type %d", file, type);
		break;
	}

	if (filepath_add(&fpt, file, talid, mtime, ok) == 0)
		errx(1, "%s: File already in tree", file);

done:
	free(file);
	entity_queue--;
}

static void
rrdp_process(struct ibuf *b)
{
	enum rrdp_msg type;
	enum publish_type pt;
	struct rrdp_session *s;
	char *uri, *last_mod, *data;
	char hash[SHA256_DIGEST_LENGTH];
	size_t dsz;
	unsigned int id;
	int ok;

	io_read_buf(b, &type, sizeof(type));
	io_read_buf(b, &id, sizeof(id));

	switch (type) {
	case RRDP_END:
		io_read_buf(b, &ok, sizeof(ok));
		rrdp_finish(id, ok);
		break;
	case RRDP_HTTP_REQ:
		io_read_str(b, &uri);
		io_read_opt_str(b, &last_mod);
		rrdp_http_fetch(id, uri, last_mod);
		free(uri);
		free(last_mod);
		break;
	case RRDP_SESSION:
		s = rrdp_session_read(b);
		rrdp_session_save(id, s);
		rrdp_session_free(s);
		break;
	case RRDP_FILE:
		io_read_buf(b, &pt, sizeof(pt));
		if (pt != PUB_ADD)
			io_read_buf(b, &hash, sizeof(hash));
		io_read_str(b, &uri);
		io_read_buf_alloc(b, (void **)&data, &dsz);

		if (uri == NULL || (pt != PUB_DEL && dsz == 0))
			errx(1, "bad rrdp file request");

		ok = rrdp_handle_file(id, pt, uri, hash, sizeof(hash),
		    data, dsz);
		rrdp_file_resp(id, ok);

		free(uri);
		free(data);
		break;
	case RRDP_CLEAR:
		rrdp_clear(id);
		break;
	default:
		errx(1, "unexpected rrdp response");
	}
}

static void
sum_stats(const struct repo *rp, const struct repotalstats *in, void *arg)
{
	struct repotalstats *out = arg;

	out->mfts += in->mfts;
	out->mfts_fail += in->mfts_fail;
	out->mfts_gap += in->mfts_gap;
	out->certs += in->certs;
	out->certs_fail += in->certs_fail;
	out->certs_nonfunc += in->certs_nonfunc;
	out->roas += in->roas;
	out->roas_fail += in->roas_fail;
	out->roas_invalid += in->roas_invalid;
	out->aspas += in->aspas;
	out->aspas_fail += in->aspas_fail;
	out->aspas_invalid += in->aspas_invalid;
	out->brks += in->brks;
	out->crls += in->crls;
	out->gbrs += in->gbrs;
	out->taks += in->taks;
	out->vrps += in->vrps;
	out->vrps_uniqs += in->vrps_uniqs;
	out->vaps += in->vaps;
	out->vaps_uniqs += in->vaps_uniqs;
	out->vaps_pas += in->vaps_pas;
	out->vaps_overflowed += in->vaps_overflowed;
	out->spls += in->spls;
	out->spls_fail += in->spls_fail;
	out->spls_invalid += in->spls_invalid;
	out->vsps += in->vsps;
	out->vsps_uniqs += in->vsps_uniqs;
}

static void
sum_repostats(const struct repo *rp, const struct repostats *in, void *arg)
{
	struct repostats *out = arg;

	out->del_files += in->del_files;
	out->extra_files += in->extra_files;
	out->del_extra_files += in->del_extra_files;
	out->del_dirs += in->del_dirs;
	out->new_files += in->new_files;
	timespecadd(&in->sync_time, &out->sync_time, &out->sync_time);
}

/*
 * Assign filenames ending in ".tal" in "/etc/rpki" into "tals",
 * returning the number of files found and filled-in.
 * This may be zero.
 * Don't exceed "max" filenames.
 */
static int
tal_load_default(void)
{
	static const char *confdir = "/etc/rpki";
	int s = 0;
	char *path;
	DIR *dirp;
	struct dirent *dp;

	dirp = opendir(confdir);
	if (dirp == NULL)
		err(1, "open %s", confdir);
	while ((dp = readdir(dirp)) != NULL) {
		if (fnmatch("*.tal", dp->d_name, FNM_PERIOD) == FNM_NOMATCH)
			continue;
		if (s >= TALSZ_MAX)
			err(1, "too many tal files found in %s",
			    confdir);
		if (asprintf(&path, "%s/%s", confdir, dp->d_name) == -1)
			err(1, NULL);
		tals[s++] = path;
	}
	closedir(dirp);
	return s;
}

/*
 * Load the list of FQDNs from the skiplist which are to be distrusted.
 * Return 0 on success.
 */
static void
load_skiplist(const char *slf)
{
	struct fqdnlistentry	*le;
	FILE			*fp;
	char			*line = NULL;
	size_t			 linesize = 0, linelen;

	if ((fp = fopen(slf, "r")) == NULL) {
		if (errno == ENOENT && strcmp(slf, DEFAULT_SKIPLIST_FILE) == 0)
			return;
		err(1, "failed to open %s", slf);
	}

	while (getline(&line, &linesize, fp) != -1) {
		/* just eat comment lines or empty lines*/
		if (line[0] == '#' || line[0] == '\n')
			continue;

		if (line[0] == ' ' || line[0] == '\t')
			errx(1, "invalid entry in skiplist: %s", line);

		/*
		 * Ignore anything after comment sign, whitespaces,
		 * also chop off LF or CR.
		 */
		linelen = strcspn(line, " #\r\n\t");
		line[linelen] = '\0';

		if (!valid_uri(line, linelen, NULL))
			errx(1, "invalid entry in skiplist: %s", line);

		if ((le = malloc(sizeof(struct fqdnlistentry))) == NULL)
			err(1, NULL);
		if ((le->fqdn = strdup(line)) == NULL)
			err(1, NULL);

		LIST_INSERT_HEAD(&skiplist, le, entry);
		stats.skiplistentries++;
	}
	if (ferror(fp))
		err(1, "error reading %s", slf);

	fclose(fp);
	free(line);
}

/*
 * Load shortlist entries.
 */
static void
load_shortlist(const char *fqdn)
{
	struct fqdnlistentry	*le;

	if (!valid_uri(fqdn, strlen(fqdn), NULL))
		errx(1, "invalid fqdn passed to -q: %s", fqdn);

	if ((le = malloc(sizeof(struct fqdnlistentry))) == NULL)
		err(1, NULL);

	if ((le->fqdn = strdup(fqdn)) == NULL)
		err(1, NULL);

	LIST_INSERT_HEAD(&shortlist, le, entry);
}

static void
check_fs_size(int fd, const char *cachedir)
{
	struct statvfs		fs;
	unsigned long long	minsize = 500 * 1024 * 1024;
	unsigned long long	minnode = 300 * 1000;

	if (fstatvfs(fd, &fs) == -1)
		err(1, "statfs %s", cachedir);

	if (fs.f_bavail < minsize / fs.f_frsize ||
	    (fs.f_ffree > 0 && fs.f_favail < minnode)) {
		fprintf(stderr, "WARNING: rpki-client may need more than "
		    "the available disk space\n"
		    "on the file-system holding %s.\n", cachedir);
		fprintf(stderr, "available space: %llukB, "
		    "suggested minimum %llukB\n",
		    (unsigned long long)fs.f_bavail * fs.f_frsize / 1024,
		    minsize / 1024);
		fprintf(stderr, "available inodes: %llu, "
		    "suggested minimum: %llu\n\n",
		    (unsigned long long)fs.f_favail, minnode);
		fflush(stderr);
	}
}

static pid_t
process_start(const char *title, int *fd)
{
	int		 fl = SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK;
	pid_t		 pid;
	int		 pair[2];

	if (socketpair(AF_UNIX, fl, 0, pair) == -1)
		err(1, "socketpair");
	if ((pid = fork()) == -1)
		err(1, "fork");

	if (pid == 0) {
		setproctitle("%s", title);
		/* change working directory to the cache directory */
		if (fchdir(cachefd) == -1)
			err(1, "fchdir");
		if (!filemode && timeout > 0)
			alarm(timeout);
		close(pair[1]);
		*fd = pair[0];
	} else {
		close(pair[0]);
		*fd = pair[1];
	}
	return pid;
}

void
sighandler(int sig)
{
	if (sig == SIGINFO)
		printinfo = 1;
	else
		killme = 1;
}

#define NPFD	4

int
main(int argc, char *argv[])
{
	int		 rc, c, i, st, hangup = 0;
	int		 procfd, rsyncfd, httpfd, rrdpfd;
	int		 nthreads = 2;
	pid_t		 pid, procpid, rsyncpid, httppid, rrdppid;
	struct pollfd	 pfd[NPFD];
	struct msgbuf	*queues[NPFD];
	struct ibuf	*b;
	char		*rsync_prog = "openrsync";
	char		*bind_addr = NULL;
	const char	*cachedir = NULL, *outputdir = NULL;
	const char	*errs, *name;
	const char	*skiplistfile = NULL;
	struct rusage	 ru;
	struct timespec	 start_time, now_time;
	struct validation_data vd = { 0 };

	clock_gettime(CLOCK_MONOTONIC, &start_time);

	RB_INIT(&vd.vrps);
	RB_INIT(&vd.brks);
	RB_INIT(&vd.vaps);
	RB_INIT(&vd.vsps);
	RB_INIT(&vd.ncas);
	RB_INIT(&vd.ccr.mfts);
	RB_INIT(&vd.ccr.vrps);
	RB_INIT(&vd.ccr.tas);

	/* If started as root, priv-drop to _rpki-client */
	if (getuid() == 0) {
		struct passwd *pw;

		pw = getpwnam("_rpki-client");
		if (!pw)
			errx(1, "no _rpki-client user to revoke to");
		if (setgroups(1, &pw->pw_gid) == -1 ||
		    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) == -1 ||
		    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) == -1)
			err(1, "unable to revoke privs");
	}
	cachedir = RPKI_PATH_BASE_DIR;
	outputdir = RPKI_PATH_OUT_DIR;
	repo_timeout = timeout / 4;
	skiplistfile = DEFAULT_SKIPLIST_FILE;

	if (pledge("stdio rpath wpath cpath inet fattr dns sendfd recvfd "
	    "proc exec unveil", NULL) == -1)
		err(1, "pledge");

	while ((c =
	    getopt(argc, argv, "0Ab:Bcd:e:fH:jmnop:P:Rs:S:t:vVx")) != -1)
		switch (c) {
		case '0':
			excludeas0 = 0;
			break;
		case 'A':
			excludeaspa = 1;
			break;
		case 'b':
			bind_addr = optarg;
			break;
		case 'B':
			outformats |= FORMAT_BIRD;
			break;
		case 'c':
			outformats |= FORMAT_CSV;
			break;
		case 'd':
			cachedir = optarg;
			break;
		case 'e':
			rsync_prog = optarg;
			break;
		case 'f':
			filemode = 1;
			noop = 1;
			break;
		case 'H':
			shortlistmode = 1;
			load_shortlist(optarg);
			break;
		case 'j':
			outformats |= FORMAT_JSON;
			break;
		case 'm':
			outformats |= FORMAT_OMETRIC;
			break;
		case 'n':
			noop = 1;
			break;
		case 'o':
			outformats |= FORMAT_OPENBGPD;
			break;
		case 'p':
			nthreads = strtonum(optarg, 1, 128, &errs);
			if (errs)
				errx(1, "-p: %s", errs);
			break;
		case 'P':
			evaluation_time = strtonum(optarg, X509_TIME_MIN + 1,
			    X509_TIME_MAX, &errs);
			if (errs)
				errx(1, "-P: time in seconds %s", errs);
			break;
		case 'R':
			rrdpon = 0;
			break;
		case 's':
			timeout = strtonum(optarg, 0, 24*60*60, &errs);
			if (errs)
				errx(1, "-s: %s", errs);
			if (timeout == 0)
				repo_timeout = 24*60*60;
			else
				repo_timeout = timeout / 4;
			break;
		case 'S':
			skiplistfile = optarg;
			break;
		case 't':
			if (talsz >= TALSZ_MAX)
				err(1, "too many tal files specified");
			tals[talsz++] = optarg;
			break;
		case 'v':
			verbose++;
			break;
		case 'V':
			fprintf(stderr, "rpki-client %s\n", RPKI_VERSION);
			return 0;
		case 'x':
			experimental = 1;
			break;
		default:
			goto usage;
		}

	argv += optind;
	argc -= optind;

	if (!filemode) {
		if (argc == 1)
			outputdir = argv[0];
		else if (argc > 1)
			goto usage;

		if (outputdir == NULL) {
			warnx("output directory required");
			goto usage;
		}
	} else {
		if (argc == 0)
			goto usage;
		outputdir = NULL;
	}

	if (cachedir == NULL) {
		warnx("cache directory required");
		goto usage;
	}

	signal(SIGPIPE, SIG_IGN);

	if ((cachefd = open(cachedir, O_RDONLY | O_DIRECTORY)) == -1)
		err(1, "cache directory %s", cachedir);
	if (outputdir != NULL) {
		if ((outdirfd = open(outputdir, O_RDONLY | O_DIRECTORY)) == -1)
			err(1, "output directory %s", outputdir);
		if (outformats == 0)
			outformats = FORMAT_OPENBGPD;
		outformats |= FORMAT_CCR;
	}

	check_fs_size(cachefd, cachedir);

	if (talsz == 0)
		talsz = tal_load_default();
	if (talsz == 0)
		err(1, "no TAL files found in %s", "/etc/rpki");

	/* Load optional constraint files sitting next to the TALs. */
	constraints_load();

	/*
	 * Create the file reader as a jailed child process.
	 * It will be responsible for reading all of the files (ROAs,
	 * manifests, certificates, etc.) and returning contents.
	 */

	procpid = process_start("parser", &procfd);
	if (procpid == 0) {
		if (!filemode)
			proc_parser(procfd, nthreads);
		else
			proc_filemode(procfd);
	}

	/* Constraints are only needed in the filemode and parser processes. */
	constraints_unload();

	/*
	 * Create a process that will do the rsync'ing.
	 * This process is responsible for making sure that all the
	 * repositories referenced by a certificate manifest (or the
	 * TAL) exists and has been downloaded.
	 */

	if (!noop) {
		rsyncpid = process_start("rsync", &rsyncfd);
		if (rsyncpid == 0) {
			close(procfd);
			proc_rsync(rsync_prog, bind_addr, rsyncfd);
		}
	} else {
		rsyncfd = -1;
		rsyncpid = -1;
	}

	/*
	 * Create a process that will fetch data via https.
	 * With every request the http process receives a file descriptor
	 * where the data should be written to.
	 */

	if (!noop && rrdpon) {
		httppid = process_start("http", &httpfd);

		if (httppid == 0) {
			close(procfd);
			close(rsyncfd);
			proc_http(bind_addr, httpfd);
		}
	} else {
		httpfd = -1;
		httppid = -1;
	}

	/*
	 * Create a process that will process RRDP.
	 * The rrdp process requires the http process to fetch the various
	 * XML files and does this via the main process.
	 */

	if (!noop && rrdpon) {
		rrdppid = process_start("rrdp", &rrdpfd);
		if (rrdppid == 0) {
			close(procfd);
			close(rsyncfd);
			close(httpfd);
			proc_rrdp(rrdpfd);
		}
	} else {
		rrdpfd = -1;
		rrdppid = -1;
	}

	if (!filemode)
		signal(SIGINFO, sighandler);
	if (!filemode && timeout > 0) {
		/*
		 * Commit suicide eventually
		 * cron will normally start a new one
		 */
		alarm(timeout);
		signal(SIGALRM, sighandler);

		/* give up a bit before the hard timeout and try to finish up */
		if (!noop)
			deadline = getmonotime() + timeout - repo_timeout / 2;
	}

	if (pledge("stdio rpath wpath cpath fattr sendfd unveil", NULL) == -1)
		err(1, "pledge");

	if ((procq = msgbuf_new_reader(sizeof(size_t), io_parse_hdr, NULL)) ==
	    NULL)
		err(1, NULL);
	if ((rsyncq = msgbuf_new_reader(sizeof(size_t), io_parse_hdr, NULL)) ==
	    NULL)
		err(1, NULL);
	if ((httpq = msgbuf_new_reader(sizeof(size_t), io_parse_hdr, NULL)) ==
	    NULL)
		err(1, NULL);
	if ((rrdpq = msgbuf_new_reader(sizeof(size_t), io_parse_hdr, NULL)) ==
	    NULL)
		err(1, NULL);

	/*
	 * The main process drives the top-down scan to leaf ROAs using
	 * data downloaded by the rsync process and parsed by the
	 * parsing process.
	 */

	pfd[0].fd = procfd;
	queues[0] = procq;
	pfd[1].fd = rsyncfd;
	queues[1] = rsyncq;
	pfd[2].fd = httpfd;
	queues[2] = httpq;
	pfd[3].fd = rrdpfd;
	queues[3] = rrdpq;

	load_skiplist(skiplistfile);

	/*
	 * Prime the process with our TAL files.
	 * These will (hopefully) contain links to manifests and we
	 * can get the ball rolling.
	 */

	for (i = 0; i < talsz; i++)
		queue_add_file(tals[i], RTYPE_TAL, i);

	if (filemode) {
		while (*argv != NULL)
			queue_add_file(*argv++, RTYPE_FILE, 0);

		if (unveil(cachedir, "r") == -1)
			err(1, "unveil cachedir");
	} else {
		if (unveil(outputdir, "rwc") == -1)
			err(1, "unveil outputdir");
		if (unveil(cachedir, "rwc") == -1)
			err(1, "unveil cachedir");
	}
	if (pledge("stdio rpath wpath cpath fattr sendfd", NULL) == -1)
		err(1, "unveil");

	/* change working directory to the cache directory */
	if (fchdir(cachefd) == -1)
		err(1, "fchdir");

	while (entity_queue > 0 && !killme) {
		int polltim;

		if (printinfo) {
			printinfo = 0;
			repo_printinfo(entity_queue);
		}

		for (i = 0; i < NPFD; i++) {
			pfd[i].events = POLLIN;
			if (msgbuf_queuelen(queues[i]) > 0)
				pfd[i].events |= POLLOUT;
		}

		polltim = repo_check_timeout(INFTIM);

		if (poll(pfd, NPFD, polltim) == -1) {
			if (errno == EINTR)
				continue;
			err(1, "poll");
		}

		for (i = 0; i < NPFD; i++) {
			if (pfd[i].revents & (POLLERR|POLLNVAL)) {
				warnx("poll[%d]: bad fd", i);
				hangup = 1;
			}
			if (pfd[i].revents & POLLHUP)
				hangup = 1;
			if (pfd[i].revents & POLLOUT) {
				if (msgbuf_write(pfd[i].fd, queues[i]) == -1) {
					if (errno == EPIPE)
						warnx("write[%d]: "
						    "connection closed", i);
					else
						warn("write[%d]", i);
					hangup = 1;
				}
			}
		}
		if (hangup)
			break;

		/*
		 * Check the rsync and http process.
		 * This means that one of our modules has completed
		 * downloading and we can flush the module requests into
		 * the parser process.
		 */

		if ((pfd[1].revents & POLLIN)) {
			switch (ibuf_read(pfd[1].fd, queues[1])) {
			case -1:
				err(1, "ibuf_read");
			case 0:
				errx(1, "ibuf_read: connection closed");
			}
			while ((b = io_buf_get(queues[1])) != NULL) {
				unsigned int id;
				int ok;

				io_read_buf(b, &id, sizeof(id));
				io_read_buf(b, &ok, sizeof(ok));
				rsync_finish(id, ok);
				ibuf_free(b);
			}
		}

		if ((pfd[2].revents & POLLIN)) {
			switch (ibuf_read(pfd[2].fd, queues[2])) {
			case -1:
				err(1, "ibuf_read");
			case 0:
				errx(1, "ibuf_read: connection closed");
			}
			while ((b = io_buf_get(queues[2])) != NULL) {
				unsigned int id;
				enum http_result res;
				char *last_mod;

				io_read_buf(b, &id, sizeof(id));
				io_read_buf(b, &res, sizeof(res));
				io_read_opt_str(b, &last_mod);
				http_finish(id, res, last_mod);
				free(last_mod);
				ibuf_free(b);
			}
		}

		/*
		 * Handle RRDP requests here.
		 */
		if ((pfd[3].revents & POLLIN)) {
			switch (ibuf_read(pfd[3].fd, queues[3])) {
			case -1:
				abort();
				err(1, "ibuf_read");
			case 0:
				errx(1, "ibuf_read: connection closed");
			}
			while ((b = io_buf_get(queues[3])) != NULL) {
				rrdp_process(b);
				ibuf_free(b);
			}
		}

		/*
		 * The parser has finished something for us.
		 * Dequeue these one by one.
		 */

		if ((pfd[0].revents & POLLIN)) {
			switch (ibuf_read(pfd[0].fd, queues[0])) {
			case -1:
				err(1, "ibuf_read");
			case 0:
				errx(1, "ibuf_read: connection closed");
			}
			while ((b = io_buf_get(queues[0])) != NULL) {
				entity_process(b, &vd, &stats);
				ibuf_free(b);
			}
		}
	}

	signal(SIGALRM, SIG_DFL);
	if (killme) {
		repo_printinfo(entity_queue);
		syslog(LOG_CRIT|LOG_DAEMON,
		    "excessive runtime (%d seconds), giving up", timeout);
		errx(1, "excessive runtime (%d seconds), giving up", timeout);
	}

	/*
	 * For clean-up, close the input for the parser and rsync
	 * process.
	 * This will cause them to exit, then we reap them.
	 */

	close(procfd);
	close(rsyncfd);
	close(httpfd);
	close(rrdpfd);

	rc = 0;
	for (;;) {
		pid = waitpid(WAIT_ANY, &st, 0);
		if (pid == -1) {
			if (errno == EINTR)
				continue;
			if (errno == ECHILD)
				break;
			err(1, "wait");
		}

		if (pid == procpid)
			name = "parser";
		else if (pid == rsyncpid)
			name = "rsync";
		else if (pid == httppid)
			name = "http";
		else if (pid == rrdppid)
			name = "rrdp";
		else
			name = "unknown";

		if (WIFSIGNALED(st)) {
			warnx("%s terminated signal %d", name, WTERMSIG(st));
			rc = 1;
		} else if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
			warnx("%s process exited abnormally", name);
			rc = 1;
		}
	}

	/* processing did not finish because of error */
	if (entity_queue != 0) {
		repo_printinfo(entity_queue);
		errx(1, "not all files processed, giving up");
	}

	/* if processing in filemode the process is done, no cleanup */
	if (filemode)
		return rc;

	logx("all files parsed: generating output");

	if (!noop)
		repo_cleanup(&fpt, cachefd);

	clock_gettime(CLOCK_MONOTONIC, &now_time);
	timespecsub(&now_time, &start_time, &stats.elapsed_time);
	if (getrusage(RUSAGE_SELF, &ru) == 0) {
		TIMEVAL_TO_TIMESPEC(&ru.ru_utime, &stats.user_time);
		TIMEVAL_TO_TIMESPEC(&ru.ru_stime, &stats.system_time);
	}
	if (getrusage(RUSAGE_CHILDREN, &ru) == 0) {
		struct timespec ts;

		TIMEVAL_TO_TIMESPEC(&ru.ru_utime, &ts);
		timespecadd(&stats.user_time, &ts, &stats.user_time);
		TIMEVAL_TO_TIMESPEC(&ru.ru_stime, &ts);
		timespecadd(&stats.system_time, &ts, &stats.system_time);
	}

	/* change working directory to the output directory */
	if (fchdir(outdirfd) == -1)
		err(1, "fchdir output dir");

	for (i = 0; i < talsz; i++) {
		repo_tal_stats_collect(sum_stats, i, &talstats[i]);
		repo_tal_stats_collect(sum_stats, i, &stats.repo_tal_stats);
	}
	repo_stats_collect(sum_repostats, &stats.repo_stats);

	serialize_ccr_content(&vd);

	if (outputfiles(&vd, &stats))
		rc = 1;

	printf("Processing time %lld seconds "
	    "(%lld seconds user, %lld seconds system)\n",
	    (long long)stats.elapsed_time.tv_sec,
	    (long long)stats.user_time.tv_sec,
	    (long long)stats.system_time.tv_sec);
	printf("Skiplist entries: %u\n", stats.skiplistentries);
	printf("CCR manifest state hash: %s\n", vd.ccr.mfts_hash);
	printf("CCR ROA payloads hash: %s\n", vd.ccr.vrps_hash);
	printf("CCR ASPA payloads hash: %s\n", vd.ccr.vaps_hash);
	printf("Route Origin Authorizations: %u (%u failed parse, %u "
	    "invalid)\n", stats.repo_tal_stats.roas,
	    stats.repo_tal_stats.roas_fail,
	    stats.repo_tal_stats.roas_invalid);
	printf("AS Provider Attestations: %u (%u failed parse, %u "
	    "invalid)\n", stats.repo_tal_stats.aspas,
	    stats.repo_tal_stats.aspas_fail,
	    stats.repo_tal_stats.aspas_invalid);
	if (experimental) {
		printf("Signed Prefix Lists: %u "
		    "(%u failed parse, %u invalid)\n",
		    stats.repo_tal_stats.spls, stats.repo_tal_stats.spls_fail,
		    stats.repo_tal_stats.spls_invalid);
	}
	printf("BGPsec Router Certificates: %u\n", stats.repo_tal_stats.brks);
	printf("Certificates: %u (%u invalid, %u non-functional)\n",
	    stats.repo_tal_stats.certs, stats.repo_tal_stats.certs_fail,
	    stats.repo_tal_stats.certs_nonfunc);
	printf("Trust Anchor Locators: %u (%u invalid)\n",
	    stats.tals, talsz - stats.tals);
	printf("Manifests: %u (%u failed parse, %u seqnum gaps)\n",
	    stats.repo_tal_stats.mfts, stats.repo_tal_stats.mfts_fail,
	    stats.repo_tal_stats.mfts_gap);
	printf("Certificate revocation lists: %u\n", stats.repo_tal_stats.crls);
	printf("Ghostbuster records: %u\n", stats.repo_tal_stats.gbrs);
	printf("Trust Anchor Keys: %u\n", stats.repo_tal_stats.taks);
	printf("Repositories: %u\n", stats.repos);
	printf("New files moved into validated cache: %u\n",
	    stats.repo_stats.new_files);
	printf("Cleanup: removed %u files, %u directories\n"
	    "Repository cleanup: kept %u and removed %u superfluous files\n",
	    stats.repo_stats.del_files, stats.repo_stats.del_dirs,
	    stats.repo_stats.extra_files, stats.repo_stats.del_extra_files);
	printf("VRP Entries: %u (%u unique)\n", stats.repo_tal_stats.vrps,
	    stats.repo_tal_stats.vrps_uniqs);
	printf("VAP Entries: %u (%u unique, %u overflowed)\n",
	    stats.repo_tal_stats.vaps, stats.repo_tal_stats.vaps_uniqs,
	    stats.repo_tal_stats.vaps_overflowed);
	printf("VSP Entries: %u (%u unique)\n", stats.repo_tal_stats.vsps,
	    stats.repo_tal_stats.vsps_uniqs);

	/* Memory cleanup. */
	repo_free();

	return rc;

usage:
	fprintf(stderr,
	    "usage: rpki-client [-0ABcjmnoRVvx] [-b sourceaddr] [-d cachedir]"
	    " [-e rsync_prog]\n"
	    "                   [-H fqdn] [-P posix-seconds] [-p threads]"
	    " [-S skiplist]\n"
	    "                   [-s timeout] [-t tal] [outputdir]\n"
	    "       rpki-client [-Vv] [-d cachedir] [-j] [-t tal] -f file ..."
	    "\n");
	return 1;
}
