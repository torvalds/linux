/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Dag-Erling Coïdan Smørgrav
 * Copyright (c) 2008 Marshall Kirk McKusick
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/endian.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <ufs/ufs/quota.h>

#include <errno.h>
#include <fcntl.h>
#include <fstab.h>
#include <grp.h>
#include <pwd.h>
#include <libutil.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct quotafile {
	int fd;				/* -1 means using quotactl for access */
	int accmode;			/* access mode */
	int wordsize;			/* 32-bit or 64-bit limits */
	int quotatype;			/* USRQUOTA or GRPQUOTA */
	dev_t dev;			/* device */
	char fsname[MAXPATHLEN + 1];	/* mount point of filesystem */
	char qfname[MAXPATHLEN + 1];	/* quota file if not using quotactl */
};

static const char *qfextension[] = INITQFNAMES;

/*
 * Check to see if a particular quota is to be enabled.
 */
static int
hasquota(struct fstab *fs, int type, char *qfnamep, int qfbufsize)
{
	char *opt;
	char *cp;
	struct statfs sfb;
	char buf[BUFSIZ];
	static char initname, usrname[100], grpname[100];

	/*
	 * 1) we only need one of these
	 * 2) fstab may specify a different filename
	 */
	if (!initname) {
		(void)snprintf(usrname, sizeof(usrname), "%s%s",
		    qfextension[USRQUOTA], QUOTAFILENAME);
		(void)snprintf(grpname, sizeof(grpname), "%s%s",
		    qfextension[GRPQUOTA], QUOTAFILENAME);
		initname = 1;
	}
	strcpy(buf, fs->fs_mntops);
	for (opt = strtok(buf, ","); opt; opt = strtok(NULL, ",")) {
		if ((cp = strchr(opt, '=')))
			*cp++ = '\0';
		if (type == USRQUOTA && strcmp(opt, usrname) == 0)
			break;
		if (type == GRPQUOTA && strcmp(opt, grpname) == 0)
			break;
	}
	if (!opt)
		return (0);
	/*
	 * Ensure that the filesystem is mounted.
	 */
	if (statfs(fs->fs_file, &sfb) != 0 ||
	    strcmp(fs->fs_file, sfb.f_mntonname)) {
		return (0);
	}
	if (cp) {
		strlcpy(qfnamep, cp, qfbufsize);
	} else {
		(void)snprintf(qfnamep, qfbufsize, "%s/%s.%s", fs->fs_file,
		    QUOTAFILENAME, qfextension[type]);
	}
	return (1);
}

struct quotafile *
quota_open(struct fstab *fs, int quotatype, int openflags)
{
	struct quotafile *qf;
	struct dqhdr64 dqh;
	struct group *grp;
	struct stat st;
	int qcmd, serrno = 0;
	int ufs;

	if ((qf = calloc(1, sizeof(*qf))) == NULL)
		return (NULL);
	qf->fd = -1;
	qf->quotatype = quotatype;
	strlcpy(qf->fsname, fs->fs_file, sizeof(qf->fsname));
	if (stat(qf->fsname, &st) != 0)
		goto error;
	qf->dev = st.st_dev;
	qcmd = QCMD(Q_GETQUOTASIZE, quotatype);
	ufs = strcmp(fs->fs_vfstype, "ufs") == 0;
	/*
	 * On UFS, hasquota() fills in qf->qfname. But we only care about
	 * this for UFS.  So we need to call hasquota() for UFS, first.
	 */
	if (ufs) {
		serrno = hasquota(fs, quotatype, qf->qfname,
		    sizeof(qf->qfname));
	}
	if (quotactl(qf->fsname, qcmd, 0, &qf->wordsize) == 0)
		return (qf);
	if (!ufs) {
		errno = 0;
		goto error;
	} else if (serrno == 0) {
		errno = EOPNOTSUPP;
		goto error;
	}
	qf->accmode = openflags & O_ACCMODE;
	if ((qf->fd = open(qf->qfname, qf->accmode|O_CLOEXEC)) < 0 &&
	    (openflags & O_CREAT) != O_CREAT)
		goto error;
	/* File open worked, so process it */
	if (qf->fd != -1) {
		qf->wordsize = 32;
		switch (read(qf->fd, &dqh, sizeof(dqh))) {
		case -1:
			goto error;
		case sizeof(dqh):
			if (strcmp(dqh.dqh_magic, Q_DQHDR64_MAGIC) != 0) {
				/* no magic, assume 32 bits */
				qf->wordsize = 32;
				return (qf);
			}
			if (be32toh(dqh.dqh_version) != Q_DQHDR64_VERSION ||
			    be32toh(dqh.dqh_hdrlen) != sizeof(struct dqhdr64) ||
			    be32toh(dqh.dqh_reclen) != sizeof(struct dqblk64)) {
				/* correct magic, wrong version / lengths */
				errno = EINVAL;
				goto error;
			}
			qf->wordsize = 64;
			return (qf);
		default:
			qf->wordsize = 32;
			return (qf);
		}
		/* not reached */
	}
	/* open failed, but O_CREAT was specified, so create a new file */
	if ((qf->fd = open(qf->qfname, O_RDWR|O_CREAT|O_TRUNC|O_CLOEXEC, 0)) <
	    0)
		goto error;
	qf->wordsize = 64;
	memset(&dqh, 0, sizeof(dqh));
	memcpy(dqh.dqh_magic, Q_DQHDR64_MAGIC, sizeof(dqh.dqh_magic));
	dqh.dqh_version = htobe32(Q_DQHDR64_VERSION);
	dqh.dqh_hdrlen = htobe32(sizeof(struct dqhdr64));
	dqh.dqh_reclen = htobe32(sizeof(struct dqblk64));
	if (write(qf->fd, &dqh, sizeof(dqh)) != sizeof(dqh)) {
		/* it was one we created ourselves */
		unlink(qf->qfname);
		goto error;
	}
	grp = getgrnam(QUOTAGROUP);
	fchown(qf->fd, 0, grp ? grp->gr_gid : 0);
	fchmod(qf->fd, 0640);
	return (qf);
error:
	serrno = errno;
	/* did we have an open file? */
	if (qf->fd != -1)
		close(qf->fd);
	free(qf);
	errno = serrno;
	return (NULL);
}

void
quota_close(struct quotafile *qf)
{

	if (qf->fd != -1)
		close(qf->fd);
	free(qf);
}

int
quota_on(struct quotafile *qf)
{
	int qcmd;

	qcmd = QCMD(Q_QUOTAON, qf->quotatype);
	return (quotactl(qf->fsname, qcmd, 0, qf->qfname));
}

int
quota_off(struct quotafile *qf)
{

	return (quotactl(qf->fsname, QCMD(Q_QUOTAOFF, qf->quotatype), 0, 0));
}

const char *
quota_fsname(const struct quotafile *qf)
{

	return (qf->fsname);
}

const char *
quota_qfname(const struct quotafile *qf)
{

	return (qf->qfname);
}

int
quota_check_path(const struct quotafile *qf, const char *path)
{
	struct stat st;

	if (stat(path, &st) == -1)
		return (-1);
	return (st.st_dev == qf->dev);
}

int
quota_maxid(struct quotafile *qf)
{
	struct stat st;
	int maxid;

	if (stat(qf->qfname, &st) < 0)
		return (0);
	switch (qf->wordsize) {
	case 32:
		maxid = st.st_size / sizeof(struct dqblk32) - 1;
		break;
	case 64:
		maxid = st.st_size / sizeof(struct dqblk64) - 2;
		break;
	default:
		maxid = 0;
		break;
	}
	return (maxid > 0 ? maxid : 0);
}

static int
quota_read32(struct quotafile *qf, struct dqblk *dqb, int id)
{
	struct dqblk32 dqb32;
	off_t off;

	off = id * sizeof(struct dqblk32);
	if (lseek(qf->fd, off, SEEK_SET) == -1)
		return (-1);
	switch (read(qf->fd, &dqb32, sizeof(dqb32))) {
	case 0:
		memset(dqb, 0, sizeof(*dqb));
		return (0);
	case sizeof(dqb32):
		dqb->dqb_bhardlimit = dqb32.dqb_bhardlimit;
		dqb->dqb_bsoftlimit = dqb32.dqb_bsoftlimit;
		dqb->dqb_curblocks = dqb32.dqb_curblocks;
		dqb->dqb_ihardlimit = dqb32.dqb_ihardlimit;
		dqb->dqb_isoftlimit = dqb32.dqb_isoftlimit;
		dqb->dqb_curinodes = dqb32.dqb_curinodes;
		dqb->dqb_btime = dqb32.dqb_btime;
		dqb->dqb_itime = dqb32.dqb_itime;
		return (0);
	default:
		return (-1);
	}
}

static int
quota_read64(struct quotafile *qf, struct dqblk *dqb, int id)
{
	struct dqblk64 dqb64;
	off_t off;

	off = sizeof(struct dqhdr64) + id * sizeof(struct dqblk64);
	if (lseek(qf->fd, off, SEEK_SET) == -1)
		return (-1);
	switch (read(qf->fd, &dqb64, sizeof(dqb64))) {
	case 0:
		memset(dqb, 0, sizeof(*dqb));
		return (0);
	case sizeof(dqb64):
		dqb->dqb_bhardlimit = be64toh(dqb64.dqb_bhardlimit);
		dqb->dqb_bsoftlimit = be64toh(dqb64.dqb_bsoftlimit);
		dqb->dqb_curblocks = be64toh(dqb64.dqb_curblocks);
		dqb->dqb_ihardlimit = be64toh(dqb64.dqb_ihardlimit);
		dqb->dqb_isoftlimit = be64toh(dqb64.dqb_isoftlimit);
		dqb->dqb_curinodes = be64toh(dqb64.dqb_curinodes);
		dqb->dqb_btime = be64toh(dqb64.dqb_btime);
		dqb->dqb_itime = be64toh(dqb64.dqb_itime);
		return (0);
	default:
		return (-1);
	}
}

int
quota_read(struct quotafile *qf, struct dqblk *dqb, int id)
{
	int qcmd;

	if (qf->fd == -1) {
		qcmd = QCMD(Q_GETQUOTA, qf->quotatype);
		return (quotactl(qf->fsname, qcmd, id, dqb));
	}
	switch (qf->wordsize) {
	case 32:
		return (quota_read32(qf, dqb, id));
	case 64:
		return (quota_read64(qf, dqb, id));
	default:
		errno = EINVAL;
		return (-1);
	}
	/* not reached */
}

#define CLIP32(u64) ((u64) > UINT32_MAX ? UINT32_MAX : (uint32_t)(u64))

static int
quota_write32(struct quotafile *qf, const struct dqblk *dqb, int id)
{
	struct dqblk32 dqb32;
	off_t off;

	dqb32.dqb_bhardlimit = CLIP32(dqb->dqb_bhardlimit);
	dqb32.dqb_bsoftlimit = CLIP32(dqb->dqb_bsoftlimit);
	dqb32.dqb_curblocks = CLIP32(dqb->dqb_curblocks);
	dqb32.dqb_ihardlimit = CLIP32(dqb->dqb_ihardlimit);
	dqb32.dqb_isoftlimit = CLIP32(dqb->dqb_isoftlimit);
	dqb32.dqb_curinodes = CLIP32(dqb->dqb_curinodes);
	dqb32.dqb_btime = CLIP32(dqb->dqb_btime);
	dqb32.dqb_itime = CLIP32(dqb->dqb_itime);

	off = id * sizeof(struct dqblk32);
	if (lseek(qf->fd, off, SEEK_SET) == -1)
		return (-1);
	if (write(qf->fd, &dqb32, sizeof(dqb32)) == sizeof(dqb32))
		return (0);
	return (-1);
}

static int
quota_write64(struct quotafile *qf, const struct dqblk *dqb, int id)
{
	struct dqblk64 dqb64;
	off_t off;

	dqb64.dqb_bhardlimit = htobe64(dqb->dqb_bhardlimit);
	dqb64.dqb_bsoftlimit = htobe64(dqb->dqb_bsoftlimit);
	dqb64.dqb_curblocks = htobe64(dqb->dqb_curblocks);
	dqb64.dqb_ihardlimit = htobe64(dqb->dqb_ihardlimit);
	dqb64.dqb_isoftlimit = htobe64(dqb->dqb_isoftlimit);
	dqb64.dqb_curinodes = htobe64(dqb->dqb_curinodes);
	dqb64.dqb_btime = htobe64(dqb->dqb_btime);
	dqb64.dqb_itime = htobe64(dqb->dqb_itime);

	off = sizeof(struct dqhdr64) + id * sizeof(struct dqblk64);
	if (lseek(qf->fd, off, SEEK_SET) == -1)
		return (-1);
	if (write(qf->fd, &dqb64, sizeof(dqb64)) == sizeof(dqb64))
		return (0);
	return (-1);
}

int
quota_write_usage(struct quotafile *qf, struct dqblk *dqb, int id)
{
	struct dqblk dqbuf;
	int qcmd;

	if (qf->fd == -1) {
		qcmd = QCMD(Q_SETUSE, qf->quotatype);
		return (quotactl(qf->fsname, qcmd, id, dqb));
	}
	/*
	 * Have to do read-modify-write of quota in file.
	 */
	if ((qf->accmode & O_RDWR) != O_RDWR) {
		errno = EBADF;
		return (-1);
	}
	if (quota_read(qf, &dqbuf, id) != 0)
		return (-1);
	/*
	 * Reset time limit if have a soft limit and were
	 * previously under it, but are now over it.
	 */
	if (dqbuf.dqb_bsoftlimit && id != 0 &&
	    dqbuf.dqb_curblocks < dqbuf.dqb_bsoftlimit &&
	    dqb->dqb_curblocks >= dqbuf.dqb_bsoftlimit)
		dqbuf.dqb_btime = 0;
	if (dqbuf.dqb_isoftlimit && id != 0 &&
	    dqbuf.dqb_curinodes < dqbuf.dqb_isoftlimit &&
	    dqb->dqb_curinodes >= dqbuf.dqb_isoftlimit)
		dqbuf.dqb_itime = 0;
	dqbuf.dqb_curinodes = dqb->dqb_curinodes;
	dqbuf.dqb_curblocks = dqb->dqb_curblocks;
	/*
	 * Write it back.
	 */
	switch (qf->wordsize) {
	case 32:
		return (quota_write32(qf, &dqbuf, id));
	case 64:
		return (quota_write64(qf, &dqbuf, id));
	default:
		errno = EINVAL;
		return (-1);
	}
	/* not reached */
}

int
quota_write_limits(struct quotafile *qf, struct dqblk *dqb, int id)
{
	struct dqblk dqbuf;
	int qcmd;

	if (qf->fd == -1) {
		qcmd = QCMD(Q_SETQUOTA, qf->quotatype);
		return (quotactl(qf->fsname, qcmd, id, dqb));
	}
	/*
	 * Have to do read-modify-write of quota in file.
	 */
	if ((qf->accmode & O_RDWR) != O_RDWR) {
		errno = EBADF;
		return (-1);
	}
	if (quota_read(qf, &dqbuf, id) != 0)
		return (-1);
	/*
	 * Reset time limit if have a soft limit and were
	 * previously under it, but are now over it
	 * or if there previously was no soft limit, but
	 * now have one and are over it.
	 */
	if (dqbuf.dqb_bsoftlimit && id != 0 &&
	    dqbuf.dqb_curblocks < dqbuf.dqb_bsoftlimit &&
	    dqbuf.dqb_curblocks >= dqb->dqb_bsoftlimit)
		dqb->dqb_btime = 0;
	if (dqbuf.dqb_bsoftlimit == 0 && id != 0 &&
	    dqb->dqb_bsoftlimit > 0 &&
	    dqbuf.dqb_curblocks >= dqb->dqb_bsoftlimit)
		dqb->dqb_btime = 0;
	if (dqbuf.dqb_isoftlimit && id != 0 &&
	    dqbuf.dqb_curinodes < dqbuf.dqb_isoftlimit &&
	    dqbuf.dqb_curinodes >= dqb->dqb_isoftlimit)
		dqb->dqb_itime = 0;
	if (dqbuf.dqb_isoftlimit == 0 && id !=0 &&
	    dqb->dqb_isoftlimit > 0 &&
	    dqbuf.dqb_curinodes >= dqb->dqb_isoftlimit)
		dqb->dqb_itime = 0;
	dqb->dqb_curinodes = dqbuf.dqb_curinodes;
	dqb->dqb_curblocks = dqbuf.dqb_curblocks;
	/*
	 * Write it back.
	 */
	switch (qf->wordsize) {
	case 32:
		return (quota_write32(qf, dqb, id));
	case 64:
		return (quota_write64(qf, dqb, id));
	default:
		errno = EINVAL;
		return (-1);
	}
	/* not reached */
}

/*
 * Convert a quota file from one format to another.
 */
int
quota_convert(struct quotafile *qf, int wordsize)
{
	struct quotafile *newqf;
	struct dqhdr64 dqh;
	struct dqblk dqblk;
	struct group *grp;
	int serrno, maxid, id, fd;

	/*
	 * Quotas must not be active and quotafile must be open
	 * for reading and writing.
	 */
	if ((qf->accmode & O_RDWR) != O_RDWR || qf->fd == -1) {
		errno = EBADF;
		return (-1);
	}
	if ((wordsize != 32 && wordsize != 64) ||
	     wordsize == qf->wordsize) {
		errno = EINVAL;
		return (-1);
	}
	maxid = quota_maxid(qf);
	if ((newqf = calloc(1, sizeof(*qf))) == NULL) {
		errno = ENOMEM;
		return (-1);
	}
	*newqf = *qf;
	snprintf(newqf->qfname, MAXPATHLEN + 1, "%s_%d.orig", qf->qfname,
	    qf->wordsize);
	if (rename(qf->qfname, newqf->qfname) < 0) {
		free(newqf);
		return (-1);
	}
	if ((newqf->fd = open(qf->qfname, O_RDWR|O_CREAT|O_TRUNC|O_CLOEXEC,
	    0)) < 0) {
		serrno = errno;
		goto error;
	}
	newqf->wordsize = wordsize;
	if (wordsize == 64) {
		memset(&dqh, 0, sizeof(dqh));
		memcpy(dqh.dqh_magic, Q_DQHDR64_MAGIC, sizeof(dqh.dqh_magic));
		dqh.dqh_version = htobe32(Q_DQHDR64_VERSION);
		dqh.dqh_hdrlen = htobe32(sizeof(struct dqhdr64));
		dqh.dqh_reclen = htobe32(sizeof(struct dqblk64));
		if (write(newqf->fd, &dqh, sizeof(dqh)) != sizeof(dqh)) {
			serrno = errno;
			goto error;
		}
	}
	grp = getgrnam(QUOTAGROUP);
	fchown(newqf->fd, 0, grp ? grp->gr_gid : 0);
	fchmod(newqf->fd, 0640);
	for (id = 0; id <= maxid; id++) {
		if ((quota_read(qf, &dqblk, id)) < 0)
			break;
		switch (newqf->wordsize) {
		case 32:
			if ((quota_write32(newqf, &dqblk, id)) < 0)
				break;
			continue;
		case 64:
			if ((quota_write64(newqf, &dqblk, id)) < 0)
				break;
			continue;
		default:
			errno = EINVAL;
			break;
		}
	}
	if (id < maxid) {
		serrno = errno;
		goto error;
	}
	/*
	 * Update the passed in quotafile to reference the new file
	 * of the converted format size.
	 */
	fd = qf->fd;
	qf->fd = newqf->fd;
	newqf->fd = fd;
	qf->wordsize = newqf->wordsize;
	quota_close(newqf);
	return (0);
error:
	/* put back the original file */
	(void) rename(newqf->qfname, qf->qfname);
	quota_close(newqf);
	errno = serrno;
	return (-1);
}
