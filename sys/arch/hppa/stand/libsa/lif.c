/*	$OpenBSD: lif.c,v 1.10 2004/11/22 18:41:41 mickey Exp $	*/

/*
 * Copyright (c) 1998-2004 Michael Shalayeff
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/disklabel.h>
#include "libsa.h"

extern int debug;

struct file {
	char f_buf[LIF_FILESTART];/* buffer for lif volume header and dir */
	struct lifvol *f_lp;	/* lif volume header pointer */
	struct lifdir *f_ld;	/* lif dir pointer */
	int	f_nfiles;	/* gross number for lif dir entries */

	off_t	f_seek;		/* seek pointer for file read */
	struct lifdir *f_rd;	/* lif dir pointer for readdir */

	int	f_isdir;	/* special hacky flag for '.' dir */
	int	f_count;	/* this file length */
	int	f_off;		/* this file offset */
};

int
lif_open (path, f)
	char *path;
	struct open_file *f;
{
	struct file *fp;
	struct lifdir *dp;
	char *p, *q;
	struct lif_load load;
	size_t buf_size;
	int err, l;

#ifdef LIFDEBUG
	if (debug)
		printf("lif_open(%s, %p)\n", path, f);
#endif

	fp = alloc(sizeof(*fp));
	/* XXX we're assuming here that sizeof(fp->f_buf) >= LIF_FILESTART */
	if ((err = (f->f_dev->dv_strategy)(f->f_devdata, F_READ, 0,
	    sizeof(fp->f_buf), &fp->f_buf, &buf_size)) ||
	    buf_size != sizeof(fp->f_buf)) {
#ifdef LIFDEBUG
		if (debug)
			printf("lif_open: unable to read LIF header (%d)\n", err);
#endif
	} else if ((fp->f_lp = (struct lifvol *)fp->f_buf)->vol_id == LIF_VOL_ID) {
		f->f_fsdata = fp;
		fp->f_ld = (struct lifdir *)(fp->f_buf + LIF_DIRSTART);
		fp->f_seek = 0;
		fp->f_rd = fp->f_ld;
		fp->f_nfiles = lifstob(fp->f_lp->vol_dirsize) /
			sizeof(struct lifdir);

		/* no dirs on the lif */
		for (p = path + (l = strlen(path)); p >= path; p--)
			if (*p == '/') {
				p++;
				break;
			}
		if (p > path)
			path = p;
	} else
		err = EINVAL;

	if (!err && *path != '.') {
		fp->f_isdir = 0;
		err = ENOENT;
		for (dp = fp->f_ld; dp < &fp->f_ld[fp->f_nfiles]; dp++) {
#ifdef LIFDEBUG
			if (debug)
				printf("lif_open: "
				       "%s <--> '%c%c%c%c%c%c%c%c%c%c'\n",
				       path, dp->dir_name[0], dp->dir_name[1],
				       dp->dir_name[2], dp->dir_name[3],
				       dp->dir_name[4], dp->dir_name[5],
				       dp->dir_name[6], dp->dir_name[7],
				       dp->dir_name[8], dp->dir_name[9]);
#endif
			for (p = path, q = dp->dir_name;
			     *q && *q != ' '; q++, p++)
				if (tolower(*q) != tolower(*p))
					break;
			if ((!*q || *q == ' ') && !*p) {
				err = 0;
				break;
			}
		}
		if (!err) {
			fp->f_off = lifstodb(dp->dir_addr);
			if (!(err =(f->f_dev->dv_strategy)(f->f_devdata, F_READ,
			      fp->f_off, sizeof(load), &load, &buf_size)) &&
			    buf_size == sizeof(load)) {
				/* no checksum */
				fp->f_count = load.count - sizeof(int);
				fp->f_off = dbtob(fp->f_off) + sizeof(load);
#ifdef LIFDEBUG
				if (debug)
					printf("lif_open: %u @ %u [%x]\n",
					       fp->f_count, fp->f_off,
					       load.address);
#endif
			} else if (!err)
				err = EIO;
		}
	} else
		fp->f_isdir = 1;

	if (err) {
		free (fp, sizeof(*fp));
		f->f_fsdata = NULL;
	}
#ifdef LIFDEBUG
	if (debug)
		printf("ret(%d)\n", err);
#endif
	return err;
}

int
lif_close(f)
	struct open_file *f;
{
	free (f->f_fsdata, sizeof(struct file));
	f->f_fsdata = NULL;
	return 0;
}

int
lif_read(f, buf, size, resid)
	struct open_file *f;
	void *buf;
	size_t size;
	size_t *resid;
{
	struct file *fp = (struct file *)f->f_fsdata;
	char *p;
	char bbuf[DEV_BSIZE];
	size_t bsize, count = sizeof(bbuf);
	int err = 0;
	int foff;

#ifdef LIFDEBUG
	if (debug)
		printf("lif_read(%p, %p, %u, %p)\n", f, buf, size, resid);
#endif

	for (p = bbuf; size; fp->f_seek += bsize, p += bsize) {
		twiddle();
		foff = fp->f_off + fp->f_seek;
		if (fp->f_seek >= fp->f_count ||
		    (err = (f->f_dev->dv_strategy)(f->f_devdata, F_READ,
		     btodb(foff), count, p, &bsize)))
			break;
		if (p == bbuf) {
			bsize = sizeof(bbuf) - (foff & (sizeof(bbuf) - 1));
			bsize = min(bsize, size);
			bcopy(bbuf + (foff & (sizeof(bbuf) - 1)), buf, bsize);
			p = buf;
		}
		count = size -= bsize;
	}
	if (resid)
		*resid = size;

	return err;
}

int
lif_write(f, buf, size, resid)
	struct open_file *f;
	void *buf;
	size_t size;
	size_t *resid;
{
	return EOPNOTSUPP;
}

off_t
lif_seek(f, offset, where)
	struct open_file *f;
	off_t offset;
	int where;
{
	struct file *fp = (struct file *)f->f_fsdata;

	switch (where) {
	case SEEK_SET:
		fp->f_seek = offset;
		break;
	case SEEK_CUR:
		fp->f_seek += offset;
		break;
	case SEEK_END:
		fp->f_seek = fp->f_count - offset;
		break;
	default:
		return (-1);
	}
	return (fp->f_seek);
}

int
lif_stat(f, sb)
	struct open_file *f;
	struct stat *sb;
{
	struct file *fp = (struct file *)f->f_fsdata;

	sb->st_mode = 0755 | (fp->f_isdir? S_IFDIR: 0);	/* XXX */
	sb->st_uid = 0;
	sb->st_gid = 0;
	sb->st_size = fp->f_count;
	return 0;
}

int
lif_readdir(f, name)
	struct open_file *f;
	char *name;
{
	struct file *fp = (struct file *)f->f_fsdata;
	char *p;

	if (name) {
		while ((fp->f_rd->dir_name[0] == ' ' ||
			!fp->f_rd->dir_name[0]) &&
		       (fp->f_rd - fp->f_ld) < fp->f_nfiles)
			fp->f_rd++;
		if ((fp->f_rd - fp->f_ld) >= fp->f_nfiles) {
			*name = '\0';
			return -1;
		}
		strncpy(name, fp->f_rd->dir_name, sizeof(fp->f_rd->dir_name));
		if ((p = strchr(name, ' ')))
			*p = '\0';
		fp->f_rd++;
	} else
		fp->f_rd = fp->f_ld;

	return 0;
}
