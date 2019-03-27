/*	$NetBSD: cd9660.c,v 1.5 1997/06/26 19:11:33 drochner Exp $	*/

/*
 * Copyright (C) 1996 Wolfgang Solfrank.
 * Copyright (C) 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Stand-alone ISO9660 file reading package.
 *
 * Note: This doesn't support Rock Ridge extensions, extended attributes,
 * blocksizes other than 2048 bytes, multi-extent files, etc.
 */
#include <sys/param.h>
#include <string.h>
#include <stdbool.h>
#include <sys/dirent.h>
#include <fs/cd9660/iso.h>
#include <fs/cd9660/cd9660_rrip.h>

#include "stand.h"

#define	SUSP_CONTINUATION	"CE"
#define	SUSP_PRESENT		"SP"
#define	SUSP_STOP		"ST"
#define	SUSP_EXTREF		"ER"
#define	RRIP_NAME		"NM"

typedef struct {
	ISO_SUSP_HEADER		h;
	u_char signature	[ISODCL (  5,    6)];
	u_char len_skp		[ISODCL (  7,    7)]; /* 711 */
} ISO_SUSP_PRESENT;
	
static int	buf_read_file(struct open_file *f, char **buf_p,
		    size_t *size_p);
static int	cd9660_open(const char *path, struct open_file *f);
static int	cd9660_close(struct open_file *f);
static int	cd9660_read(struct open_file *f, void *buf, size_t size,
		    size_t *resid);
static off_t	cd9660_seek(struct open_file *f, off_t offset, int where);
static int	cd9660_stat(struct open_file *f, struct stat *sb);
static int	cd9660_readdir(struct open_file *f, struct dirent *d);
static int	dirmatch(struct open_file *f, const char *path,
		    struct iso_directory_record *dp, int use_rrip, int lenskip);
static int	rrip_check(struct open_file *f, struct iso_directory_record *dp,
		    int *lenskip);
static char	*rrip_lookup_name(struct open_file *f,
		    struct iso_directory_record *dp, int lenskip, size_t *len);
static ISO_SUSP_HEADER *susp_lookup_record(struct open_file *f,
		    const char *identifier, struct iso_directory_record *dp,
		    int lenskip);

struct fs_ops cd9660_fsops = {
	"cd9660",
	cd9660_open,
	cd9660_close,
	cd9660_read,
	null_write,
	cd9660_seek,
	cd9660_stat,
	cd9660_readdir
};

#define	F_ISDIR		0x0001		/* Directory */
#define	F_ROOTDIR	0x0002		/* Root directory */
#define	F_RR		0x0004		/* Rock Ridge on this volume */

struct file {
	int 		f_flags;	/* file flags */
	off_t 		f_off;		/* Current offset within file */
	daddr_t 	f_bno;		/* Starting block number */
	off_t 		f_size;		/* Size of file */
	daddr_t		f_buf_blkno;	/* block number of data block */	
	char		*f_buf;		/* buffer for data block */
	int		f_susp_skip;	/* len_skip for SUSP records */
};

struct ptable_ent {
	char namlen	[ISODCL( 1, 1)];	/* 711 */
	char extlen	[ISODCL( 2, 2)];	/* 711 */
	char block	[ISODCL( 3, 6)];	/* 732 */
	char parent	[ISODCL( 7, 8)];	/* 722 */
	char name	[1];
};
#define	PTFIXSZ		8
#define	PTSIZE(pp)	roundup(PTFIXSZ + isonum_711((pp)->namlen), 2)

#define	cdb2devb(bno)	((bno) * ISO_DEFAULT_BLOCK_SIZE / DEV_BSIZE)

static ISO_SUSP_HEADER *
susp_lookup_record(struct open_file *f, const char *identifier,
    struct iso_directory_record *dp, int lenskip)
{
	static char susp_buffer[ISO_DEFAULT_BLOCK_SIZE];
	ISO_SUSP_HEADER *sh;
	ISO_RRIP_CONT *shc;
	char *p, *end;
	int error;
	size_t read;

	p = dp->name + isonum_711(dp->name_len) + lenskip;
	/* Names of even length have a padding byte after the name. */
	if ((isonum_711(dp->name_len) & 1) == 0)
		p++;
	end = (char *)dp + isonum_711(dp->length);
	while (p + 3 < end) {
		sh = (ISO_SUSP_HEADER *)p;
		if (bcmp(sh->type, identifier, 2) == 0)
			return (sh);
		if (bcmp(sh->type, SUSP_STOP, 2) == 0)
			return (NULL);
		if (bcmp(sh->type, SUSP_CONTINUATION, 2) == 0) {
			shc = (ISO_RRIP_CONT *)sh;
			error = f->f_dev->dv_strategy(f->f_devdata, F_READ,
			    cdb2devb(isonum_733(shc->location)),
			    ISO_DEFAULT_BLOCK_SIZE, susp_buffer, &read);

			/* Bail if it fails. */
			if (error != 0 || read != ISO_DEFAULT_BLOCK_SIZE)
				return (NULL);
			p = susp_buffer + isonum_733(shc->offset);
			end = p + isonum_733(shc->length);
		} else {
			/* Ignore this record and skip to the next. */
			p += isonum_711(sh->length);

			/* Avoid infinite loops with corrupted file systems */
			if (isonum_711(sh->length) == 0)
				return (NULL);
		}
	}
	return (NULL);
}

static char *
rrip_lookup_name(struct open_file *f, struct iso_directory_record *dp,
    int lenskip, size_t *len)
{
	ISO_RRIP_ALTNAME *p;

	if (len == NULL)
		return (NULL);

	p = (ISO_RRIP_ALTNAME *)susp_lookup_record(f, RRIP_NAME, dp, lenskip);
	if (p == NULL)
		return (NULL);
	switch (*p->flags) {
	case ISO_SUSP_CFLAG_CURRENT:
		*len = 1;
		return (".");
	case ISO_SUSP_CFLAG_PARENT:
		*len = 2;
		return ("..");
	case 0:
		*len = isonum_711(p->h.length) - 5;
		return ((char *)p + 5);
	default:
		/*
		 * We don't handle hostnames or continued names as they are
		 * too hard, so just bail and use the default name.
		 */
		return (NULL);
	}
}

static int
rrip_check(struct open_file *f, struct iso_directory_record *dp, int *lenskip)
{
	ISO_SUSP_PRESENT *sp;
	ISO_RRIP_EXTREF *er;
	char *p;

	/* First, see if we can find a SP field. */
	p = dp->name + isonum_711(dp->name_len);
	if (p > (char *)dp + isonum_711(dp->length))
		return (0);
	sp = (ISO_SUSP_PRESENT *)p;
	if (bcmp(sp->h.type, SUSP_PRESENT, 2) != 0)
		return (0);
	if (isonum_711(sp->h.length) != sizeof(ISO_SUSP_PRESENT))
		return (0);
	if (sp->signature[0] != 0xbe || sp->signature[1] != 0xef)
		return (0);
	*lenskip = isonum_711(sp->len_skp);

	/*
	 * Now look for an ER field.  If RRIP is present, then there must
	 * be at least one of these.  It would be more pedantic to walk
	 * through the list of fields looking for a Rock Ridge ER field.
	 */
	er = (ISO_RRIP_EXTREF *)susp_lookup_record(f, SUSP_EXTREF, dp, 0);
	if (er == NULL)
		return (0);
	return (1);
}

static int
dirmatch(struct open_file *f, const char *path, struct iso_directory_record *dp,
    int use_rrip, int lenskip)
{
	size_t len, plen;
	char *cp, *sep;
	int i, icase;

	if (use_rrip)
		cp = rrip_lookup_name(f, dp, lenskip, &len);
	else
		cp = NULL;
	if (cp == NULL) {
		len = isonum_711(dp->name_len);
		cp = dp->name;
		icase = 1;
	} else
		icase = 0;

	sep = strchr(path, '/');
	if (sep != NULL) {
		plen = sep - path;
	} else {
		plen = strlen(path);
	}

	if (plen != len)
		return (0);

	for (i = len; --i >= 0; path++, cp++) {
		if (!*path || *path == '/')
			break;
		if (*path == *cp)
			continue;
		if (!icase && toupper(*path) == *cp)
			continue;
		return 0;
	}
	if (*path && *path != '/')
		return 0;
	/*
	 * Allow stripping of trailing dots and the version number.
	 * Note that this will find the first instead of the last version
	 * of a file.
	 */
	if (i >= 0 && (*cp == ';' || *cp == '.')) {
		/* This is to prevent matching of numeric extensions */
		if (*cp == '.' && cp[1] != ';')
			return 0;
		while (--i >= 0)
			if (*++cp != ';' && (*cp < '0' || *cp > '9'))
				return 0;
	}
	return 1;
}

static int
cd9660_open(const char *path, struct open_file *f)
{
	struct file *fp = NULL;
	void *buf;
	struct iso_primary_descriptor *vd;
	size_t buf_size, read, dsize, off;
	daddr_t bno, boff;
	struct iso_directory_record rec;
	struct iso_directory_record *dp = NULL;
	int rc, first, use_rrip, lenskip;
	bool isdir = false;

	/* First find the volume descriptor */
	buf = malloc(buf_size = ISO_DEFAULT_BLOCK_SIZE);
	vd = buf;
	for (bno = 16;; bno++) {
		twiddle(1);
		rc = f->f_dev->dv_strategy(f->f_devdata, F_READ, cdb2devb(bno),
					ISO_DEFAULT_BLOCK_SIZE, buf, &read);
		if (rc)
			goto out;
		if (read != ISO_DEFAULT_BLOCK_SIZE) {
			rc = EIO;
			goto out;
		}
		rc = EINVAL;
		if (bcmp(vd->id, ISO_STANDARD_ID, sizeof vd->id) != 0)
			goto out;
		if (isonum_711(vd->type) == ISO_VD_END)
			goto out;
		if (isonum_711(vd->type) == ISO_VD_PRIMARY)
			break;
	}
	if (isonum_723(vd->logical_block_size) != ISO_DEFAULT_BLOCK_SIZE)
		goto out;

	bcopy(vd->root_directory_record, &rec, sizeof(rec));
	if (*path == '/') path++; /* eat leading '/' */

	first = 1;
	use_rrip = 0;
	lenskip = 0;
	while (*path) {
		bno = isonum_733(rec.extent) + isonum_711(rec.ext_attr_length);
		dsize = isonum_733(rec.size);
		off = 0;
		boff = 0;

		while (off < dsize) {
			if ((off % ISO_DEFAULT_BLOCK_SIZE) == 0) {
				twiddle(1);
				rc = f->f_dev->dv_strategy
					(f->f_devdata, F_READ,
					 cdb2devb(bno + boff),
					 ISO_DEFAULT_BLOCK_SIZE,
					 buf, &read);
				if (rc)
					goto out;
				if (read != ISO_DEFAULT_BLOCK_SIZE) {
					rc = EIO;
					goto out;
				}
				boff++;
				dp = (struct iso_directory_record *) buf;
			}
			if (isonum_711(dp->length) == 0) {
			    /* skip to next block, if any */
			    off = boff * ISO_DEFAULT_BLOCK_SIZE;
			    continue;
			}

			/* See if RRIP is in use. */
			if (first)
				use_rrip = rrip_check(f, dp, &lenskip);

			if (dirmatch(f, path, dp, use_rrip,
			    first ? 0 : lenskip)) {
				first = 0;
				break;
			} else
				first = 0;

			dp = (struct iso_directory_record *)
				((char *) dp + isonum_711(dp->length));
			/* If the new block has zero length, it is padding. */
			if (isonum_711(dp->length) == 0) {
				/* Skip to next block, if any. */
				off = boff * ISO_DEFAULT_BLOCK_SIZE;
				continue;
			}
			off += isonum_711(dp->length);
		}
		if (off >= dsize) {
			rc = ENOENT;
			goto out;
		}

		rec = *dp;
		while (*path && *path != '/') /* look for next component */
			path++;

		if (*path)	/* this component was directory */
			isdir = true;

		while (*path == '/')
			path++;	/* skip '/' */

		if (*path)	/* We do have next component. */
			isdir = false;
	}

	/*
	 * if the path had trailing / but the path does point to file,
	 * report the error ENOTDIR.
	 */
	if (isdir == true && (isonum_711(rec.flags) & 2) == 0) {
		rc = ENOTDIR;
		goto out;
	}

	/* allocate file system specific data structure */
	fp = malloc(sizeof(struct file));
	bzero(fp, sizeof(struct file));
	f->f_fsdata = (void *)fp;

	if ((isonum_711(rec.flags) & 2) != 0) {
		fp->f_flags = F_ISDIR;
	}
	if (first) {
		fp->f_flags |= F_ROOTDIR;

		/* Check for Rock Ridge since we didn't in the loop above. */
		bno = isonum_733(rec.extent) + isonum_711(rec.ext_attr_length);
		twiddle(1);
		rc = f->f_dev->dv_strategy(f->f_devdata, F_READ, cdb2devb(bno),
		    ISO_DEFAULT_BLOCK_SIZE, buf, &read);
		if (rc)
			goto out;
		if (read != ISO_DEFAULT_BLOCK_SIZE) {
			rc = EIO;
			goto out;
		}
		dp = (struct iso_directory_record *)buf;
		use_rrip = rrip_check(f, dp, &lenskip);
	}
	if (use_rrip) {
		fp->f_flags |= F_RR;
		fp->f_susp_skip = lenskip;
	}
	fp->f_off = 0;
	fp->f_bno = isonum_733(rec.extent) + isonum_711(rec.ext_attr_length);
	fp->f_size = isonum_733(rec.size);
	free(buf);

	return 0;

out:
	if (fp)
		free(fp);
	free(buf);

	return rc;
}

static int
cd9660_close(struct open_file *f)
{
	struct file *fp = (struct file *)f->f_fsdata;

	f->f_fsdata = NULL;
	free(fp);

	return 0;
}

static int
buf_read_file(struct open_file *f, char **buf_p, size_t *size_p)
{
	struct file *fp = (struct file *)f->f_fsdata;
	daddr_t blkno, blkoff;
	int rc = 0;
	size_t read;

	blkno = fp->f_off / ISO_DEFAULT_BLOCK_SIZE + fp->f_bno;
	blkoff = fp->f_off % ISO_DEFAULT_BLOCK_SIZE;

	if (blkno != fp->f_buf_blkno) {
		if (fp->f_buf == (char *)0)
			fp->f_buf = malloc(ISO_DEFAULT_BLOCK_SIZE);

		twiddle(16);
		rc = f->f_dev->dv_strategy(f->f_devdata, F_READ,
		    cdb2devb(blkno), ISO_DEFAULT_BLOCK_SIZE,
		    fp->f_buf, &read);
		if (rc)
			return (rc);
		if (read != ISO_DEFAULT_BLOCK_SIZE)
			return (EIO);

		fp->f_buf_blkno = blkno;
	}

	*buf_p = fp->f_buf + blkoff;
	*size_p = ISO_DEFAULT_BLOCK_SIZE - blkoff;

	if (*size_p > fp->f_size - fp->f_off)
		*size_p = fp->f_size - fp->f_off;
	return (rc);
}

static int
cd9660_read(struct open_file *f, void *start, size_t size, size_t *resid)
{
	struct file *fp = (struct file *)f->f_fsdata;
	char *buf, *addr;
	size_t buf_size, csize;
	int rc = 0;

	addr = start;
	while (size) {
		if (fp->f_off < 0 || fp->f_off >= fp->f_size)
			break;

		rc = buf_read_file(f, &buf, &buf_size);
		if (rc)
			break;

		csize = size > buf_size ? buf_size : size;
		bcopy(buf, addr, csize);

		fp->f_off += csize;
		addr += csize;
		size -= csize;
	}
	if (resid)
		*resid = size;
	return (rc);
}

static int
cd9660_readdir(struct open_file *f, struct dirent *d)
{
	struct file *fp = (struct file *)f->f_fsdata;
	struct iso_directory_record *ep;
	size_t buf_size, reclen, namelen;
	int error = 0;
	int lenskip;
	char *buf, *name;

again:
	if (fp->f_off >= fp->f_size)
		return (ENOENT);
	error = buf_read_file(f, &buf, &buf_size);
	if (error)
		return (error);
	ep = (struct iso_directory_record *)buf;

	if (isonum_711(ep->length) == 0) {
		daddr_t blkno;
		
		/* skip to next block, if any */
		blkno = fp->f_off / ISO_DEFAULT_BLOCK_SIZE;
		fp->f_off = (blkno + 1) * ISO_DEFAULT_BLOCK_SIZE;
		goto again;
	}

	if (fp->f_flags & F_RR) {
		if (fp->f_flags & F_ROOTDIR && fp->f_off == 0)
			lenskip = 0;
		else
			lenskip = fp->f_susp_skip;
		name = rrip_lookup_name(f, ep, lenskip, &namelen);
	} else
		name = NULL;
	if (name == NULL) {
		namelen = isonum_711(ep->name_len);
		name = ep->name;
		if (namelen == 1) {
			if (ep->name[0] == 0)
				name = ".";
			else if (ep->name[0] == 1) {
				namelen = 2;
				name = "..";
			}
		}
	}
	reclen = sizeof(struct dirent) - (MAXNAMLEN+1) + namelen + 1;
	reclen = (reclen + 3) & ~3;

	d->d_fileno = isonum_733(ep->extent);
	d->d_reclen = reclen;
	if (isonum_711(ep->flags) & 2)
		d->d_type = DT_DIR;
	else
		d->d_type = DT_REG;
	d->d_namlen = namelen;

	bcopy(name, d->d_name, d->d_namlen);
	d->d_name[d->d_namlen] = 0;

	fp->f_off += isonum_711(ep->length);
	return (0);
}

static off_t
cd9660_seek(struct open_file *f, off_t offset, int where)
{
	struct file *fp = (struct file *)f->f_fsdata;

	switch (where) {
	case SEEK_SET:
		fp->f_off = offset;
		break;
	case SEEK_CUR:
		fp->f_off += offset;
		break;
	case SEEK_END:
		fp->f_off = fp->f_size - offset;
		break;
	default:
		return -1;
	}
	return fp->f_off;
}

static int
cd9660_stat(struct open_file *f, struct stat *sb)
{
	struct file *fp = (struct file *)f->f_fsdata;

	/* only important stuff */
	sb->st_mode = S_IRUSR | S_IRGRP | S_IROTH;
	if (fp->f_flags & F_ISDIR)
		sb->st_mode |= S_IFDIR;
	else
		sb->st_mode |= S_IFREG;
	sb->st_uid = sb->st_gid = 0;
	sb->st_size = fp->f_size;
	return 0;
}
