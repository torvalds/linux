/*	$OpenBSD: queue_fs.c,v 1.23 2023/05/31 16:51:46 op Exp $	*/

/*
 * Copyright (c) 2011 Gilles Chehade <gilles@poolp.org>
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
#include <sys/mount.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <inttypes.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "smtpd.h"
#include "log.h"

#define PATH_QUEUE		"/queue"
#define PATH_INCOMING		"/incoming"
#define PATH_EVPTMP		PATH_INCOMING "/envelope.tmp"
#define PATH_MESSAGE		"/message"

/* percentage of remaining space / inodes required to accept new messages */
#define	MINSPACE		5
#define	MININODES		5

struct qwalk {
	FTS	*fts;
	int	 depth;
};

static int	fsqueue_check_space(void);
static void	fsqueue_envelope_path(uint64_t, char *, size_t);
static void	fsqueue_envelope_incoming_path(uint64_t, char *, size_t);
static int	fsqueue_envelope_dump(char *, const char *, size_t, int, int);
static void	fsqueue_message_path(uint32_t, char *, size_t);
static void	fsqueue_message_incoming_path(uint32_t, char *, size_t);
static void    *fsqueue_qwalk_new(void);
static int	fsqueue_qwalk(void *, uint64_t *);
static void	fsqueue_qwalk_close(void *);

struct tree evpcount;
static struct timespec startup;

#define REF	(int*)0xf00

static int
queue_fs_message_create(uint32_t *msgid)
{
	char		rootdir[PATH_MAX];
	struct stat	sb;

	if (!fsqueue_check_space())
		return 0;

again:
	*msgid = queue_generate_msgid();

	/* prevent possible collision later when moving to Q_QUEUE */
	fsqueue_message_path(*msgid, rootdir, sizeof(rootdir));
	if (stat(rootdir, &sb) != -1)
		goto again;

	/* we hit an unexpected error, temporarily fail */
	if (errno != ENOENT) {
		*msgid = 0;
		return 0;
	}

	fsqueue_message_incoming_path(*msgid, rootdir, sizeof(rootdir));
	if (mkdir(rootdir, 0700) == -1) {
		if (errno == EEXIST)
			goto again;

		if (errno == ENOSPC) {
			*msgid = 0;
			return 0;
		}

		log_warn("warn: queue-fs: mkdir");
		*msgid = 0;
		return 0;
	}

	return (1);
}

static int
queue_fs_message_commit(uint32_t msgid, const char *path)
{
	char incomingdir[PATH_MAX];
	char queuedir[PATH_MAX];
	char msgdir[PATH_MAX];
	char msgpath[PATH_MAX];

	/* before-first, move the message content in the incoming directory */
	fsqueue_message_incoming_path(msgid, msgpath, sizeof(msgpath));
	if (strlcat(msgpath, PATH_MESSAGE, sizeof(msgpath))
	    >= sizeof(msgpath))
		return (0);
	if (rename(path, msgpath) == -1)
		return (0);

	fsqueue_message_incoming_path(msgid, incomingdir, sizeof(incomingdir));
	fsqueue_message_path(msgid, msgdir, sizeof(msgdir));
	if (strlcpy(queuedir, msgdir, sizeof(queuedir))
	    >= sizeof(queuedir))
		return (0);

	/* first attempt to rename */
	if (rename(incomingdir, msgdir) == 0)
		return 1;
	if (errno == ENOSPC)
		return 0;
	if (errno != ENOENT) {
		log_warn("warn: queue-fs: rename");
		return 0;
	}

	/* create the bucket */
	*strrchr(queuedir, '/') = '\0';
	if (mkdir(queuedir, 0700) == -1) {
		if (errno == ENOSPC)
			return 0;
		if (errno != EEXIST) {
			log_warn("warn: queue-fs: mkdir");
			return 0;
		}
	}

	/* rename */
	if (rename(incomingdir, msgdir) == -1) {
		if (errno == ENOSPC)
			return 0;
		log_warn("warn: queue-fs: rename");
		return 0;
	}

	return 1;
}

static int
queue_fs_message_fd_r(uint32_t msgid)
{
	int fd;
	char path[PATH_MAX];

	fsqueue_message_path(msgid, path, sizeof(path));
	if (strlcat(path, PATH_MESSAGE, sizeof(path))
	    >= sizeof(path))
		return -1;

	if ((fd = open(path, O_RDONLY)) == -1) {
		log_warn("warn: queue-fs: open");
		return -1;
	}

	return fd;
}

static int
queue_fs_message_delete(uint32_t msgid)
{
	char		path[PATH_MAX];
	struct stat	sb;

	fsqueue_message_incoming_path(msgid, path, sizeof(path));
	if (stat(path, &sb) == -1)
		fsqueue_message_path(msgid, path, sizeof(path));

	if (rmtree(path, 0) == -1)
		log_warn("warn: queue-fs: rmtree");

	tree_pop(&evpcount, msgid);

	return 1;
}

static int
queue_fs_envelope_create(uint32_t msgid, const char *buf, size_t len,
    uint64_t *evpid)
{
	char		path[PATH_MAX];
	int		queued = 0, i, r = 0, *n;
	struct stat	sb;

	if (msgid == 0) {
		log_warnx("warn: queue-fs: msgid=0, evpid=%016"PRIx64, *evpid);
		goto done;
	}

	fsqueue_message_incoming_path(msgid, path, sizeof(path));
	if (stat(path, &sb) == -1)
		queued = 1;

	for (i = 0; i < 20; i ++) {
		*evpid = queue_generate_evpid(msgid);
		if (queued)
			fsqueue_envelope_path(*evpid, path, sizeof(path));
		else
			fsqueue_envelope_incoming_path(*evpid, path,
			    sizeof(path));

		if ((r = fsqueue_envelope_dump(path, buf, len, 0, 0)) != 0)
			goto done;
	}
	r = 0;
	log_warnx("warn: queue-fs: could not allocate evpid");

done:
	if (r) {
		n = tree_pop(&evpcount, msgid);
		if (n == NULL)
			n = REF;
		n += 1;
		tree_xset(&evpcount, msgid, n);
	}
	return (r);
}

static int
queue_fs_envelope_load(uint64_t evpid, char *buf, size_t len)
{
	char	 pathname[PATH_MAX];
	FILE	*fp;
	size_t	 r;

	fsqueue_envelope_path(evpid, pathname, sizeof(pathname));

	fp = fopen(pathname, "r");
	if (fp == NULL) {
		if (errno != ENOENT && errno != ENFILE)
			log_warn("warn: queue-fs: fopen");
		return 0;
	}

	r = fread(buf, 1, len, fp);
	if (r) {
		if (r == len) {
			log_warn("warn: queue-fs: too large");
			r = 0;
		}
		else
			buf[r] = '\0';
	}
	fclose(fp);

	return (r);
}

static int
queue_fs_envelope_update(uint64_t evpid, const char *buf, size_t len)
{
	char dest[PATH_MAX];

	fsqueue_envelope_path(evpid, dest, sizeof(dest));

	return (fsqueue_envelope_dump(dest, buf, len, 1, 1));
}

static int
queue_fs_envelope_delete(uint64_t evpid)
{
	char		pathname[PATH_MAX];
	uint32_t	msgid;
	int		*n;

	fsqueue_envelope_path(evpid, pathname, sizeof(pathname));
	if (unlink(pathname) == -1)
		if (errno != ENOENT)
			return 0;

	msgid = evpid_to_msgid(evpid);
	n = tree_pop(&evpcount, msgid);
	n -= 1;

	if (n - REF == 0)
		queue_fs_message_delete(msgid);
	else
		tree_xset(&evpcount, msgid, n);

	return (1);
}

static int
queue_fs_message_walk(uint64_t *evpid, char *buf, size_t len,
    uint32_t msgid, int *done, void **data)
{
	struct dirent	*dp;
	DIR		*dir = *data;
	char		 path[PATH_MAX];
	char		 msgid_str[9];
	char		*tmp;
	int		 r, *n;

	if (*done)
		return (-1);

	if (!bsnprintf(path, sizeof path, "%s/%02x/%08x",
	    PATH_QUEUE, (msgid  & 0xff000000) >> 24, msgid))
		fatalx("queue_fs_message_walk: path does not fit buffer");

	if (dir == NULL) {
		if ((dir = opendir(path)) == NULL) {
			log_warn("warn: queue_fs: opendir: %s", path);
			*done = 1;
			return (-1);
		}

		*data = dir;
	}

	(void)snprintf(msgid_str, sizeof msgid_str, "%08" PRIx32, msgid);
	while ((dp = readdir(dir)) != NULL) {
		if (dp->d_type != DT_REG)
			continue;

		/* ignore files other than envelopes */
		if (strlen(dp->d_name) != 16 ||
		    strncmp(dp->d_name, msgid_str, 8))
			continue;

		tmp = NULL;
		*evpid = strtoull(dp->d_name, &tmp, 16);
		if (tmp && *tmp !=  '\0') {
			log_debug("debug: fsqueue: bogus file %s", dp->d_name);
			continue;
		}

		memset(buf, 0, len);
		r = queue_fs_envelope_load(*evpid, buf, len);
		if (r) {
			n = tree_pop(&evpcount, msgid);
			if (n == NULL)
				n = REF;

			n += 1;
			tree_xset(&evpcount, msgid, n);
		}

		return (r);
	}

	(void)closedir(dir);
	*done = 1;
	return (-1);
}

static int
queue_fs_envelope_walk(uint64_t *evpid, char *buf, size_t len)
{
	static int	 done = 0;
	static void	*hdl = NULL;
	int		 r, *n;
	uint32_t	 msgid;

	if (done)
		return (-1);

	if (hdl == NULL)
		hdl = fsqueue_qwalk_new();

	if (fsqueue_qwalk(hdl, evpid)) {
		memset(buf, 0, len);
		r = queue_fs_envelope_load(*evpid, buf, len);
		if (r) {
			msgid = evpid_to_msgid(*evpid);
			n = tree_pop(&evpcount, msgid);
			if (n == NULL)
				n = REF;
			n += 1;
			tree_xset(&evpcount, msgid, n);
		}
		return (r);
	}

	fsqueue_qwalk_close(hdl);
	done = 1;
	return (-1);
}

static int
fsqueue_check_space(void)
{
	struct statfs	buf;
	uint64_t	used;
	uint64_t	total;

	if (statfs(PATH_QUEUE, &buf) == -1) {
		log_warn("warn: queue-fs: statfs");
		return 0;
	}

	/*
	 * f_bfree and f_ffree is not set on all filesystems.
	 * They could be signed or unsigned integers.
	 * Some systems will set them to 0, others will set them to -1.
	 */
	if (buf.f_bfree == 0 || buf.f_ffree == 0 ||
	    (int64_t)buf.f_bfree == -1 || (int64_t)buf.f_ffree == -1)
		return 1;

	used = buf.f_blocks - buf.f_bfree;
	total = buf.f_bavail + used;
	if (total != 0)
		used = (float)used / (float)total * 100;
	else
		used = 100;
	if (100 - used < MINSPACE) {
		log_warnx("warn: not enough disk space: %llu%% left",
		    (unsigned long long) 100 - used);
		log_warnx("warn: temporarily rejecting messages");
		return 0;
	}

	used = buf.f_files - buf.f_ffree;
	total = buf.f_favail + used;
	if (total != 0)
		used = (float)used / (float)total * 100;
	else
		used = 100;
	if (100 - used < MININODES) {
		log_warnx("warn: not enough inodes: %llu%% left",
		    (unsigned long long) 100 - used);
		log_warnx("warn: temporarily rejecting messages");
		return 0;
	}

	return 1;
}

static void
fsqueue_envelope_path(uint64_t evpid, char *buf, size_t len)
{
	if (!bsnprintf(buf, len, "%s/%02x/%08x/%016" PRIx64,
		PATH_QUEUE,
		(evpid_to_msgid(evpid) & 0xff000000) >> 24,
		evpid_to_msgid(evpid),
		evpid))
		fatalx("fsqueue_envelope_path: path does not fit buffer");
}

static void
fsqueue_envelope_incoming_path(uint64_t evpid, char *buf, size_t len)
{
	if (!bsnprintf(buf, len, "%s/%08x/%016" PRIx64,
		PATH_INCOMING,
		evpid_to_msgid(evpid),
		evpid))
		fatalx("fsqueue_envelope_incoming_path: path does not fit buffer");
}

static int
fsqueue_envelope_dump(char *dest, const char *evpbuf, size_t evplen,
    int do_atomic, int do_sync)
{
	const char     *path = do_atomic ? PATH_EVPTMP : dest;
	FILE	       *fp = NULL;
	int		fd;
	size_t		w;

	if ((fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600)) == -1) {
		log_warn("warn: queue-fs: open");
		goto tempfail;
	}

	if ((fp = fdopen(fd, "w")) == NULL) {
		log_warn("warn: queue-fs: fdopen");
		goto tempfail;
	}

	w = fwrite(evpbuf, 1, evplen, fp);
	if (w < evplen) {
		log_warn("warn: queue-fs: short write");
		goto tempfail;
	}
	if (fflush(fp)) {
		log_warn("warn: queue-fs: fflush");
		goto tempfail;
	}
	if (do_sync && fsync(fileno(fp))) {
		log_warn("warn: queue-fs: fsync");
		goto tempfail;
	}
	if (fclose(fp) != 0) {
		log_warn("warn: queue-fs: fclose");
		fp = NULL;
		goto tempfail;
	}
	fp = NULL;
	fd = -1;

	if (do_atomic && rename(path, dest) == -1) {
		log_warn("warn: queue-fs: rename");
		goto tempfail;
	}
	return (1);

tempfail:
	if (fp)
		fclose(fp);
	else if (fd != -1)
		close(fd);
	if (unlink(path) == -1)
		log_warn("warn: queue-fs: unlink");
	return (0);
}

static void
fsqueue_message_path(uint32_t msgid, char *buf, size_t len)
{
	if (!bsnprintf(buf, len, "%s/%02x/%08x",
		PATH_QUEUE,
		(msgid & 0xff000000) >> 24,
		msgid))
		fatalx("fsqueue_message_path: path does not fit buffer");
}

static void
fsqueue_message_incoming_path(uint32_t msgid, char *buf, size_t len)
{
	if (!bsnprintf(buf, len, "%s/%08x",
		PATH_INCOMING,
		msgid))
		fatalx("fsqueue_message_incoming_path: path does not fit buffer");
}

static void *
fsqueue_qwalk_new(void)
{
	char		 path[PATH_MAX];
	char * const	 path_argv[] = { path, NULL };
	struct qwalk	*q;

	q = xcalloc(1, sizeof(*q));
	(void)strlcpy(path, PATH_QUEUE, sizeof(path));
	q->fts = fts_open(path_argv,
	    FTS_PHYSICAL | FTS_NOCHDIR, NULL);

	if (q->fts == NULL)
		fatal("fsqueue_qwalk_new: fts_open: %s", path);

	return (q);
}

static void
fsqueue_qwalk_close(void *hdl)
{
	struct qwalk	*q = hdl;

	fts_close(q->fts);

	free(q);
}

static int
fsqueue_qwalk(void *hdl, uint64_t *evpid)
{
	struct qwalk	*q = hdl;
	FTSENT		*e;
	char		*tmp;

	while ((e = fts_read(q->fts)) != NULL) {
		switch (e->fts_info) {
		case FTS_D:
			q->depth += 1;
			if (q->depth == 2 && e->fts_namelen != 2) {
				log_debug("debug: fsqueue: bogus directory %s",
				    e->fts_path);
				fts_set(q->fts, e, FTS_SKIP);
				break;
			}
			if (q->depth == 3 && e->fts_namelen != 8) {
				log_debug("debug: fsqueue: bogus directory %s",
				    e->fts_path);
				fts_set(q->fts, e, FTS_SKIP);
				break;
			}
			break;

		case FTS_DP:
		case FTS_DNR:
			q->depth -= 1;
			break;

		case FTS_F:
			if (q->depth != 3)
				break;
			if (e->fts_namelen != 16)
				break;
			if (timespeccmp(&e->fts_statp->st_mtim, &startup, >))
				break;
			tmp = NULL;
			*evpid = strtoull(e->fts_name, &tmp, 16);
			if (tmp && *tmp !=  '\0') {
				log_debug("debug: fsqueue: bogus file %s",
				    e->fts_path);
				break;
			}
			return (1);
		default:
			break;
		}
	}

	return (0);
}

static int
queue_fs_init(struct passwd *pw, int server, const char *conf)
{
	unsigned int	 n;
	char		*paths[] = { PATH_QUEUE, PATH_INCOMING };
	char		 path[PATH_MAX];
	int		 ret;

	/* remove incoming/ if it exists */
	if (server)
		mvpurge(PATH_SPOOL PATH_INCOMING, PATH_SPOOL PATH_PURGE);

	fsqueue_envelope_path(0, path, sizeof(path));

	ret = 1;
	for (n = 0; n < nitems(paths); n++) {
		(void)strlcpy(path, PATH_SPOOL, sizeof(path));
		if (strlcat(path, paths[n], sizeof(path)) >= sizeof(path))
			fatalx("path too long %s%s", PATH_SPOOL, paths[n]);
		if (ckdir(path, 0700, pw->pw_uid, 0, server) == 0)
			ret = 0;
	}

	if (clock_gettime(CLOCK_REALTIME, &startup))
		fatal("clock_gettime");

	tree_init(&evpcount);

	queue_api_on_message_create(queue_fs_message_create);
	queue_api_on_message_commit(queue_fs_message_commit);
	queue_api_on_message_delete(queue_fs_message_delete);
	queue_api_on_message_fd_r(queue_fs_message_fd_r);
	queue_api_on_envelope_create(queue_fs_envelope_create);
	queue_api_on_envelope_delete(queue_fs_envelope_delete);
	queue_api_on_envelope_update(queue_fs_envelope_update);
	queue_api_on_envelope_load(queue_fs_envelope_load);
	queue_api_on_envelope_walk(queue_fs_envelope_walk);
	queue_api_on_message_walk(queue_fs_message_walk);

	return (ret);
}

struct queue_backend	queue_backend_fs = {
	queue_fs_init,
};
