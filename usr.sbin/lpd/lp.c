/*	$OpenBSD: lp.c,v 1.4 2022/12/28 21:30:17 jmc Exp $	*/

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
#include <sys/mount.h>
#include <sys/stat.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lp.h"

#include "log.h"

#define XXX_PID_MAX 99999

struct qentry {
	char *name;
	time_t mtime;
};

static int readent(struct lp_printer *, char *);
static int qentrycmp(const void *, const void *);
static int fullpath(struct lp_printer *, const char *, char *, size_t);
static int checksize(struct lp_printer *, size_t);

static int scanning;
static char *db[] = {
	_PATH_PRINTCAP,
	NULL,
};

/*
 * Fill a printer structure from its /etc/printcap entry.
 * Return 0 on success, or -1 on error.
 */
int
lp_getprinter(struct lp_printer *lp, const char *name)
{
	char *buf = NULL;
	int r;

	memset(lp, 0, sizeof(*lp));

	r = cgetent(&buf, db, name);
	if (r == 0)
		r = readent(lp, buf);
	free(buf);

	switch (r) {
	case -3:
		log_warnx("%s: potential reference loop", name);
		break;
	case -2:
		log_warn("%s", name);
		break;
	case -1:
		log_warnx("%s: unknown printer", name);
		break;
	case 0:
		return 0;
	case 1:
		log_warnx("%s: unresolved tc expansion", name);
		break;
	default:
		log_warnx("%s: unexpected return value %d", name, r);
	}

	lp_clearprinter(lp);
	return -1;
}

/*
 * Iterate over /etc/printcap and fill the printer structure from the next
 * entry, if any.
 *
 * Return 0 if no entry is found.
 *        1 if a printer is filled.
 *       -1 on error, and set errno.
 */
int
lp_scanprinters(struct lp_printer *lp)
{
	char *buf;
	int r, saved_errno;

	if (scanning++ == 0)
		r = cgetfirst(&buf, db);
	else
		r = cgetnext(&buf, db);

	if (r == 0) {
		cgetclose();
		scanning = 0;
		return 0;
	} else if (r == 1) {
		memset(lp, 0, sizeof(*lp));
		r = readent(lp, buf);
		free(buf);
		if (r == -2)
			goto fail;
		return 1;
	} else if (r == -1) 
		fatal("cannot open %s", _PATH_PRINTCAP);
	else if (r == -2)
		errno = ELOOP; /* potential reference loop */

    fail:
	saved_errno = errno;
	lp_clearprinter(lp);
	cgetclose();
	scanning = 0;
	errno = saved_errno;
	return -1;
}

static int
readent(struct lp_printer *lp, char *buf)
{
	char *s;

	s = strchr(buf, ':');
	if (s)
		*s = '\0';
	lp->lp_name = strdup(buf);
	if (s)
		*s = ':';
	if (lp->lp_name == NULL)
		return -2;

	s = lp->lp_name;
	while ((s = strchr(s, '|'))) {
		*s++ = '\0';
		if (lp->lp_aliascount < LP_MAXALIASES)
			lp->lp_aliases[lp->lp_aliascount++] = s;
	}

#define CGETSTR(x)	if (cgetstr(buf, #x, &(lp->lp_ ## x)) == -2) \
				goto fail
#define CGETNUM(x, d)	if (cgetnum(buf, #x, &(lp->lp_ ## x)) == -1) \
				(lp->lp_ ## x) = d;
#define CGETBOOL(x) lp->lp_ ## x = cgetcap(buf, #x, ':') ? 1 : 0

	CGETSTR(af);
	CGETNUM(br, 0);
	CGETSTR(cf);
	CGETSTR(df);
	CGETSTR(ff);
	CGETBOOL(fo);
	CGETSTR(gf);
	CGETBOOL(hl);
	CGETBOOL(ic);
	CGETSTR(if);
	CGETSTR(lf);
	CGETSTR(lo);
	CGETSTR(lp);
	CGETNUM(mc, 0);
	CGETSTR(ms);
	CGETNUM(mx, 0);
	CGETSTR(nd);
	CGETSTR(nf);
	CGETSTR(of);
	CGETNUM(pc, DEFAULT_PC);
	CGETNUM(pl, DEFAULT_PL);
	CGETNUM(pw, DEFAULT_PW);
	CGETNUM(px, 0);
	CGETNUM(py, 0);
	CGETSTR(rf);
	CGETSTR(rg);
	CGETSTR(rm);
	CGETSTR(rp);
	CGETBOOL(rs);
	CGETBOOL(rw);
	CGETBOOL(sb);
	CGETBOOL(sc);
	CGETSTR(sd);
	CGETBOOL(sf);
	CGETBOOL(sh);
	CGETSTR(st);
	CGETSTR(tf);
	CGETSTR(tr);
	CGETSTR(vf);

	if (lp->lp_rm && lp->lp_rm[0]) {
		lp->lp_type = PRN_LPR;
		lp->lp_host = lp->lp_rm;
		lp->lp_port = NULL;
	}
	else if (strchr(LP_LP(lp), '@')) {
		lp->lp_type = PRN_NET;
		lp->lp_port = strdup(LP_LP(lp));
		lp->lp_host = strchr(lp->lp_port, '@');
		*lp->lp_host++ = '\0';
	}
	else {
		lp->lp_type = PRN_LOCAL;
	}

	return 0;
    fail:
	return -2;
}

void
lp_clearprinter(struct lp_printer *lp)
{
	free(lp->lp_name);
	free(lp->lp_port);
	if (lp->lp_lock)
		fclose(lp->lp_lock);
	free(lp->lp_af);
	free(lp->lp_cf);
	free(lp->lp_df);
	free(lp->lp_ff);
	free(lp->lp_gf);
	free(lp->lp_if);
	free(lp->lp_lf);
	free(lp->lp_lo);
	free(lp->lp_lp);
	free(lp->lp_ms);
	free(lp->lp_nd);
	free(lp->lp_nf);
	free(lp->lp_of);
	free(lp->lp_rf);
	free(lp->lp_rg);
	free(lp->lp_rm);
	free(lp->lp_rp);
	free(lp->lp_sd);
	free(lp->lp_st);
	free(lp->lp_tf);
	free(lp->lp_tr);
	free(lp->lp_vf);
	memset(lp, 0, sizeof(*lp));
}

static int
qentrycmp(const void *aa, const void *bb)
{
	const struct qentry *a = aa, *b = bb;

	if (a->mtime < b->mtime)
		return -1;
	if (a->mtime > b->mtime)
		return 1;

	return strcmp(a->name, b->name);
}

/*
 * Read the printer queue content.
 * Return the task count, or -1 on error.
 */
int
lp_readqueue(struct lp_printer *lp, struct lp_queue *q)
{
	struct qentry *qe = NULL, *tqe;
	struct dirent *d;
	struct stat st;
	size_t len, sz, nqi, nqe = 0, nqa = 0, n, i;
	char path[PATH_MAX], *end;
	DIR *dp= NULL;

	len = strlcpy(path, LP_SD(lp), sizeof(path));
	if (len == 0 || len >= sizeof(path)) {
		log_warn("%s: %s: invalid spool directory name",
		    __func__, LP_SD(lp));
		goto fail;
	}

	if ((dp = opendir(path)) == NULL) {
		log_warn("%s: opendir", __func__);
		goto fail;
	}

	if (fstat(dirfd(dp), &st) == -1) {
		log_warn("%s: fstat", __func__);
		goto fail;
	}
	/* Assume half the files are cf files. */
	nqi = st.st_nlink / 2;

	if (path[len-1] != '/') {
		len = strlcat(path, "/", sizeof(path));
		if (len >= sizeof(path)) {
			errno = ENAMETOOLONG;
			log_warn("%s: strlcat", __func__);
			goto fail;
		}
	}
	end = path + len;
	sz = sizeof(path) - len;

	errno = 0;
	while ((d = readdir(dp))) {
		if (d->d_name[0] != 'c' || d->d_name[1] != 'f')
			continue;

		if (strlen(d->d_name) < 7)
			continue;

		if (!isdigit((unsigned char)d->d_name[3]) ||
		    !isdigit((unsigned char)d->d_name[4]) ||
		    !isdigit((unsigned char)d->d_name[5]))
			continue;

		if (strlcpy(end, d->d_name, sz) >= sz) {
			errno = ENAMETOOLONG;
			log_warn("%s: strlcat", __func__);
			goto fail;
		}

		if (stat(path, &st) == -1) {
			log_warn("%s: stat", __func__);
			goto fail;
		}

		if (nqe == nqa) {
			n = nqa ? (nqa + 5) : nqi;
			tqe = recallocarray(qe, nqa, n, sizeof(*qe));
			if (tqe == NULL) {
				log_warn("%s: recallocarray", __func__);
				goto fail;
			}
			qe = tqe;
			nqa = n;
		}

		if ((qe[nqe].name = strdup(d->d_name)) == NULL) {
			log_warn("%s: strdup", __func__);
			goto fail;
		}
		qe[nqe].mtime = st.st_mtime;
		nqe++;
	}
	if (errno) {
		log_warn("%s: readdir", __func__);
		goto fail;
	}
	closedir(dp);
	dp = NULL;

	qsort(qe, nqe, sizeof(*qe), qentrycmp);

	q->count = nqe;
	q->cfname = calloc(nqe, sizeof(*q->cfname));
	if (q->cfname == NULL) {
		log_warn("%s: calloc", __func__);
		goto fail;
	}
	for (i = 0; i < nqe; i++)
		q->cfname[i] = qe[i].name;

	free(qe);
	return nqe;

    fail:
	if (dp)
		closedir(dp);
	for (i = 0; i < nqe; i++)
		free(qe[i].name);
	free(qe);
	lp_clearqueue(q);
	return -1;
}

void
lp_clearqueue(struct lp_queue *q)
{
	int i;

	for (i = 0; i < q->count; i++)
		free(q->cfname[i]);
	free(q->cfname);
}

static int
fullpath(struct lp_printer *lp, const char *fname, char *dst, size_t sz)
{
	int r;

	r = snprintf(dst, sz, "%s/%s", LP_SD(lp), fname);
	if (r < 0 || (size_t)r >= sz) {
		errno = ENAMETOOLONG;
		return -1;
	}

	return 0;
}

/*
 * fopen(3) a file in the printer spooldir for reading.
 */
FILE *
lp_fopen(struct lp_printer *lp, const char *fname)
{
	char path[PATH_MAX];

	if (fullpath(lp, fname, path, sizeof(path)) == -1)
		return NULL;

	return fopen(path, "r");
}

/*
 * unlink(2) a file in the printer spooldir.
 */
int
lp_unlink(struct lp_printer *lp, const char *fname)
{
	char path[PATH_MAX];

	if (fullpath(lp, fname, path, sizeof(path)) == -1)
		return -1;

	return unlink(path);
}

/*
 * stat(2) a file in the printer spooldir.
 */
int
lp_stat(struct lp_printer *lp, const char *fname, struct stat *st)
{
	char path[PATH_MAX];

	if (fullpath(lp, fname, path, sizeof(path)) == -1)
		return -1;

	return stat(path, st);
}

/*
 * Grab the lockfile for this printer, and associate it to the printer.
 * Return -1 on error or 0 otherwise.
 */
int
lp_lock(struct lp_printer *lp)
{
	char path[PATH_MAX];
	struct stat st;
        int fd, saved_errno;

	if (lp->lp_lock) {
		errno = EWOULDBLOCK;
		return -1;
	}

	if (fullpath(lp, LP_LO(lp), path, sizeof(path)) == -1) {
		log_warn("%s: %s", __func__, LP_LO(lp));
		return -1;
	}

        fd = open(path, O_WRONLY|O_CREAT|O_NOFOLLOW|O_NONBLOCK|O_EXLOCK|O_TRUNC,
	    0640);
	if (fd == -1) {
		if (errno != EWOULDBLOCK)
			log_warn("%s: open", __func__);
		return -1;
	}

	if (fstat(fd, &st) == -1) {
		log_warn("%s: fstat", __func__);
		saved_errno = errno;
		(void)close(fd);
		errno = saved_errno;
		return -1;
        }

	if (!S_ISREG(st.st_mode)) {
		errno = EFTYPE;
		log_warnx("%s: %s: Not a regular file", __func__, path);
		(void)close(fd);
		return -1;
	}

	if ((lp->lp_lock = fdopen(fd, "w")) == NULL) {
		log_warn("%s: fdopen", __func__);
		saved_errno = errno;
		(void)close(fd);
		errno = saved_errno;
		return -1;
	}

	lp_setcurrtask(lp, NULL);

	return 0;
}

/*
 * Truncate the lock file and close it.
 */
void
lp_unlock(struct lp_printer *lp)
{
	if (ftruncate(fileno(lp->lp_lock), 0) == -1)
		log_warn("%s: ftruncate", __func__);
	if (fclose(lp->lp_lock) == EOF)
		log_warn("%s: fclose", __func__);
	lp->lp_lock = NULL;
}

int
lp_getqueuestate(struct lp_printer *lp, int reset, int *qstate)
{
	FILE *fp;
	struct stat st;
	int saved_errno;

	*qstate = 0;

	fp = lp->lp_lock;
	if (lp->lp_lock == NULL) {
		fp = lp_fopen(lp, LP_LO(lp));
		if (fp == NULL) {
			if (errno == ENOENT)
				return 0;
			log_warn("%s: lp_fopen", __func__);
			return -1;
		}
	}

	if (fstat(fileno(fp), &st) == -1) {
		log_warn("%s: fstat", __func__);
		if (lp->lp_lock == NULL) {
			saved_errno = errno;
			(void)fclose(fp);
			errno = saved_errno;
		}
		return -1;
	}

	if (st.st_mode & S_IXUSR)
		*qstate |= LPQ_PRINTER_DOWN;
	if (st.st_mode & S_IXGRP)
		*qstate |= LPQ_QUEUE_OFF;
	if (st.st_mode & S_IXOTH) {
		*qstate |= LPQ_QUEUE_UPDATED;
		if (reset) {
			st.st_mode &= ~S_IXOTH & 0777;
			if (fchmod(fileno(lp->lp_lock), st.st_mode) == -1)
				log_warn("%s: fchmod", __func__);
		}
	}

	if (lp->lp_lock == NULL)
		fclose(fp);

	return 0;
}

/*
 * Update the current task description in the lock file.
 * The lock file must be opened.
 */
void
lp_setcurrtask(struct lp_printer *lp, const char *cfile)
{
	int r;

	if (ftruncate(fileno(lp->lp_lock), 0) == -1)
		log_warn("%s: ftruncate", __func__);

	if (cfile)
		r = fprintf(lp->lp_lock, "%d\n%s\n", (int)getpid(), cfile);
	else
		r = fprintf(lp->lp_lock, "%d\n", (int)getpid());
	if (r < 0)
		log_warn("%s: fprintf", __func__);

	if (fflush(lp->lp_lock) != 0)
		log_warn("%s: fflush", __func__);
}

/*
 * Find the pid of the running process if any, and the task being printed.
 *
 * Returns -1 on error (errno set).
 *          0 if no process is running.
 *          1 if a printer process is up.
 *          2 if a printer process is up and a file is being printed.
 */
int
lp_getcurrtask(struct lp_printer *lp, pid_t *ppid, char *dst, size_t dstsz)
{
	FILE *fp;
	const char *errstr;
	char *line = NULL;
	size_t linesz = 0;
	ssize_t len;
	pid_t pid;
	int r, saved_errno;

	pid = *ppid = 0;
	dst[0] = '\0';
	r = -1;

	fp = lp_fopen(lp, LP_LO(lp));
	if (fp == NULL) {
		if (errno == ENOENT)
			return 0;
		log_warn("%s: lp_fopen", __func__);
		return -1;
	}

	while ((len = getline(&line, &linesz, fp)) != -1) {
		if (line[len-1] == '\n')
			line[len-1] = '\0';

		/* Read filename. */
		if (pid) {
			(void)strlcpy(dst, line, dstsz);
			break;
		}

		pid = strtonum(line, 2, XXX_PID_MAX, &errstr);
		if (errstr) {
			log_warn("%s: strtonum", __func__);
			goto done;
		}
	}

	if ((errno = ferror(fp))) {
		log_warn("%s: getline", __func__);
		goto done;
	}

	r = 0;
	if (pid == 0)
		goto done;

	if (kill(pid, 0) == -1 && errno != EPERM) {
		if (errno != ESRCH)
			log_warn("%s: kill", __func__);
		goto done;
	}

	*ppid = pid;
	r = dst[0] ? 2 : 1;

    done:
	free(line);
	saved_errno = errno;
	(void)fclose(fp);
	errno = saved_errno;
	return r;
}

/*
 * Read the current printer status file.
 * Return -1 on error, 0 on success.
 */
int
lp_getstatus(struct lp_printer *lp, char *buf, size_t bufsz)
{
	size_t len;
	char *line;
	FILE *fp;
	int saved_errno;

	buf[0] = '\0';

	fp = lp_fopen(lp, LP_ST(lp));
	if (fp == NULL) {
		if (errno == ENOENT)
			return 0;
		log_warn("%s: lp_fopen", __func__);
		return -1;
	}

	line = fgetln(fp, &len);
	if (line) {
		if (len >= bufsz)
			len = bufsz - 1;
		else if (line[len - 1] == '\n')
			len--;
		memmove(buf, line, len);
		buf[len] = '\0';
	}
	else if ((errno = ferror(fp))) {
		log_warn("%s: fgetln", __func__);
		saved_errno = errno;
		(void)fclose(fp);
		errno = saved_errno;
		return -1;
	}

	(void)fclose(fp);
	return 0;
}

/*
 * Update the current printer status.
 */
void
lp_setstatus(struct lp_printer *lp, const char *fmt, ...)
{
	va_list ap;
	FILE *fp;
	char path[PATH_MAX], *s;
	int fd = -1, r, saved_errno;

	va_start(ap, fmt);
	r = vasprintf(&s, fmt, ap);
	va_end(ap);

	if (r == -1) {
		log_warn("%s: vasprintf", __func__);
		return;
	}

	if (fullpath(lp, LP_ST(lp), path, sizeof(path)) == -1) {
		log_warn("%s: fullpath", __func__);
		free(s);
		return;
	}

	fd = open(path, O_WRONLY|O_CREAT|O_NOFOLLOW|O_EXLOCK|O_TRUNC, 0660);
	if (fd == -1) {
		log_warn("%s: open", __func__);
		free(s);
		return;
	}

	fp = fdopen(fd, "w");
	if (fp == NULL) {
		log_warn("%s: fdopen", __func__);
		saved_errno = errno;
		(void)close(fd);
		free(s);
		errno = saved_errno;
		return;
	}

	r = fprintf(fp, "%s\n", s);

	if (r <= 0) {
		log_warn("%s: fprintf", __func__);
		saved_errno = errno;
		(void)fclose(fp);
		free(s);
		errno = saved_errno;
		return;
	}

	(void)fclose(fp);
	free(s);
}

/*
 * Check that the given string is a valid filename for the spooler.
 */
int
lp_validfilename(const char *fname, int cf)
{
	size_t len, i;

	len = strlen(fname);
	if (len <= 6) /* strlen("cfA000") */
		return 0;

	if (fname[0] != (cf ? 'c' : 'd') ||
	    fname[1] != 'f' ||
	    fname[2] < 'A' ||
	    fname[2] > 'Z' ||
	    !isdigit((unsigned char)fname[3]) ||
	    !isdigit((unsigned char)fname[4]) ||
	    !isdigit((unsigned char)fname[5]))
		return 0;

	for (i = 6; i < len; i++)
		if (!isalpha((unsigned char)fname[i]) &&
		    !isdigit((unsigned char)fname[i]) &&
		    strchr("-_.", (unsigned char)fname[i]) == NULL)
			return 0;

	return 1;
}

/*
 * Create a new control or data file in the printer spooldir.
 * Control files have there name changed to a temporary name. They are renamed
 * to their final name by lp_commit().
 */
int
lp_create(struct lp_printer *lp, int cf, size_t sz, const char *fname)
{
	struct stat st;
	char path[PATH_MAX];
	int fd;

	/* Make sure the file size is acceptable. */
	if (checksize(lp, sz) == -1)
		return -1;

	if (fullpath(lp, fname, path, sizeof(path)) == -1) {
		log_warn("%s: %s", __func__, fname);
		return -1;
	}

	if (cf) {
		/*
		 * Create a temporary file, but we want to avoid
		 * a collision with the final cf filename.
		 */
		/* XXX this would require a lock on .seq */
		path[strlen(LP_SD(lp)) + 1] = 'c';
		if (stat(path, &st) == 0) {
			errno = EEXIST;
			log_warn("%s: %s", __func__, fname);
			return -1;
		}
		path[strlen(LP_SD(lp)) + 1] = 't';
	}

	fd = open(path, O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW, 0660);
	if (fd == -1)
		log_warn("%s: open", __func__);

	return fd;
}

/*
 * Commit the job given by its temporary CF name.
 * This is done by renaming the temporary CF file name to its final name.
 * The functions return 0 on success, or -1 on error and set errno.
 */
int
lp_commit(struct lp_printer *lp, const char *cf)
{
	char ipath[PATH_MAX], opath[PATH_MAX];

	if (fullpath(lp, cf, ipath, sizeof(ipath)) == -1 ||
	    fullpath(lp, cf, opath, sizeof(opath)) == -1)
		return -1;

	ipath[strlen(LP_SD(lp)) + 1] = 't';
	opath[strlen(LP_SD(lp)) + 1] = 'c';

	return rename(ipath, opath);
}

/*
 * Check if a file size is acceptable.
 * Return -1 on error or if false (EFBIG or ENOSPC), 0 otherwise.
 */
static int
checksize(struct lp_printer *lp, size_t sz)
{
	struct statfs st;
	ssize_t len;
	size_t linesz = 0;
	off_t req, avail, minfree;
	char *line = NULL;
	const char *errstr;
	FILE *fp;
	int saved_errno;

	if (sz == 0) {
		errno = EINVAL;
		return -1;
	}

	/* Check printer limit. */
	if (lp->lp_mx && sz > (size_t)lp->lp_mx) {
		errno = EFBIG;
		return -1;
	}

	/*
	 * Check for minfree.  Note that it does not really guarantee the
	 * directory will not be filled up anyway, since it's not taking
	 * other incoming files into account.
	 */
	fp = lp_fopen(lp, "minfree");
	if (fp == NULL) {
		if (errno == ENOENT)
			return 0;
		log_warn("%s: lp_fopen", __func__);
		return -1;
	}

	len = getline(&line, &linesz, fp);
	saved_errno = errno;
	(void)fclose(fp);
	if (len == -1) {
		errno = saved_errno;
		return 0;
	}

	if (line[len - 1] == '\n')
		line[len - 1] = '\0';
	minfree = strtonum(line, 0, INT_MAX, &errstr);
	free(line);

	if (errstr)
		return 0;

	if (statfs(LP_SD(lp), &st) == -1)
		return 0;

	req = sz / 512 + (sz % 512) ? 1 : 0;
	avail = st.f_bavail * (st.f_bsize / 512);
	if (avail < minfree || (avail - minfree) < req) {
		errno = ENOSPC;
		return -1;
	}

	return 0;
}
