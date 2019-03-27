/* 
 * Copyright (c) 1998 Michael Smith.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "stand.h"

#include <sys/stat.h>
#include <string.h>
#include <zlib.h>

#define Z_BUFSIZE 2048	/* XXX larger? */

struct z_file
{
    int			zf_rawfd;
    off_t		zf_dataoffset;
    z_stream		zf_zstream;
    unsigned char	zf_buf[Z_BUFSIZE];
    int			zf_endseen;
};

static int	zf_fill(struct z_file *z);
static int	zf_open(const char *path, struct open_file *f);
static int	zf_close(struct open_file *f);
static int	zf_read(struct open_file *f, void *buf, size_t size, size_t *resid);
static off_t	zf_seek(struct open_file *f, off_t offset, int where);
static int	zf_stat(struct open_file *f, struct stat *sb);

struct fs_ops gzipfs_fsops = {
    "zip",
    zf_open, 
    zf_close, 
    zf_read,
    null_write,
    zf_seek,
    zf_stat,
    null_readdir
};

static int
zf_fill(struct z_file *zf)
{
    int		result;
    int		req;
    
    req = Z_BUFSIZE - zf->zf_zstream.avail_in;
    result = 0;
    
    /* If we need more */
    if (req > 0) {
	/* move old data to bottom of buffer */
	if (req < Z_BUFSIZE)
	    bcopy(zf->zf_buf + req, zf->zf_buf, Z_BUFSIZE - req);
	
	/* read to fill buffer and update availibility data */
	result = read(zf->zf_rawfd, zf->zf_buf + zf->zf_zstream.avail_in, req);
	zf->zf_zstream.next_in = zf->zf_buf;
	if (result >= 0)
	    zf->zf_zstream.avail_in += result;
    }
    return(result);
}

/*
 * Adapted from get_byte/check_header in libz
 *
 * Returns 0 if the header is OK, nonzero if not.
 */
static int
get_byte(struct z_file *zf, off_t *curoffp)
{
    if ((zf->zf_zstream.avail_in == 0) && (zf_fill(zf) == -1))
	return(-1);
    zf->zf_zstream.avail_in--;
    ++*curoffp;
    return(*(zf->zf_zstream.next_in)++);
}

static int gz_magic[2] = {0x1f, 0x8b}; /* gzip magic header */

/* gzip flag byte */
#define ASCII_FLAG	0x01 /* bit 0 set: file probably ascii text */
#define HEAD_CRC	0x02 /* bit 1 set: header CRC present */
#define EXTRA_FIELD	0x04 /* bit 2 set: extra field present */
#define ORIG_NAME	0x08 /* bit 3 set: original file name present */
#define COMMENT		0x10 /* bit 4 set: file comment present */
#define RESERVED	0xE0 /* bits 5..7: reserved */

static int
check_header(struct z_file *zf)
{
    int		method; /* method byte */
    int		flags;  /* flags byte */
    uInt	len;
    int		c;

    zf->zf_dataoffset = 0;
    /* Check the gzip magic header */
    for (len = 0; len < 2; len++) {
	c = get_byte(zf, &zf->zf_dataoffset);
	if (c != gz_magic[len]) {
	    return(1);
	}
    }
    method = get_byte(zf, &zf->zf_dataoffset);
    flags = get_byte(zf, &zf->zf_dataoffset);
    if (method != Z_DEFLATED || (flags & RESERVED) != 0) {
	return(1);
    }
    
    /* Discard time, xflags and OS code: */
    for (len = 0; len < 6; len++) (void)get_byte(zf, &zf->zf_dataoffset);

    if ((flags & EXTRA_FIELD) != 0) { /* skip the extra field */
	len  =  (uInt)get_byte(zf, &zf->zf_dataoffset);
	len += ((uInt)get_byte(zf, &zf->zf_dataoffset))<<8;
	/* len is garbage if EOF but the loop below will quit anyway */
	while (len-- != 0 && get_byte(zf, &zf->zf_dataoffset) != -1) ;
    }
    if ((flags & ORIG_NAME) != 0) { /* skip the original file name */
	while ((c = get_byte(zf, &zf->zf_dataoffset)) != 0 && c != -1) ;
    }
    if ((flags & COMMENT) != 0) {   /* skip the .gz file comment */
	while ((c = get_byte(zf, &zf->zf_dataoffset)) != 0 && c != -1) ;
    }
    if ((flags & HEAD_CRC) != 0) {  /* skip the header crc */
	for (len = 0; len < 2; len++) c = get_byte(zf, &zf->zf_dataoffset);
    }
    /* if there's data left, we're in business */
    return((c == -1) ? 1 : 0);
}
	
static int
zf_open(const char *fname, struct open_file *f)
{
    static char		*zfname;
    int			rawfd;
    struct z_file	*zf;
    char		*cp;
    int			error;
    struct stat		sb;

    /* Have to be in "just read it" mode */
    if (f->f_flags != F_READ)
	return(EPERM);

    /* If the name already ends in .gz or .bz2, ignore it */
    if ((cp = strrchr(fname, '.')) && (!strcmp(cp, ".gz")
	    || !strcmp(cp, ".bz2") || !strcmp(cp, ".split")))
	return(ENOENT);

    /* Construct new name */
    zfname = malloc(strlen(fname) + 4);
    if (zfname == NULL)
        return(ENOMEM);
    sprintf(zfname, "%s.gz", fname);

    /* Try to open the compressed datafile */
    rawfd = open(zfname, O_RDONLY);
    free(zfname);
    if (rawfd == -1)
	return(ENOENT);

    if (fstat(rawfd, &sb) < 0) {
	printf("zf_open: stat failed\n");
	close(rawfd);
	return(ENOENT);
    }
    if (!S_ISREG(sb.st_mode)) {
	printf("zf_open: not a file\n");
	close(rawfd);
	return(EISDIR);			/* best guess */
    }

    /* Allocate a z_file structure, populate it */
    zf = malloc(sizeof(struct z_file));
    if (zf == NULL)
        return(ENOMEM);
    bzero(zf, sizeof(struct z_file));
    zf->zf_rawfd = rawfd;

    /* Verify that the file is gzipped */
    if (check_header(zf)) {
	close(zf->zf_rawfd);
	free(zf);
	return(EFTYPE);
    }

    /* Initialise the inflation engine */
    if ((error = inflateInit2(&(zf->zf_zstream), -15)) != Z_OK) {
	printf("zf_open: inflateInit returned %d : %s\n", error, zf->zf_zstream.msg);
	close(zf->zf_rawfd);
	free(zf);
	return(EIO);
    }

    /* Looks OK, we'll take it */
    f->f_fsdata = zf;
    return(0);
}

static int
zf_close(struct open_file *f)
{
    struct z_file	*zf = (struct z_file *)f->f_fsdata;
    
    inflateEnd(&(zf->zf_zstream));
    close(zf->zf_rawfd);
    free(zf);
    return(0);
}
 
static int 
zf_read(struct open_file *f, void *buf, size_t size, size_t *resid)
{
    struct z_file	*zf = (struct z_file *)f->f_fsdata;
    int			error;

    zf->zf_zstream.next_out = buf;			/* where and how much */
    zf->zf_zstream.avail_out = size;

    while (zf->zf_zstream.avail_out && zf->zf_endseen == 0) {
	if ((zf->zf_zstream.avail_in == 0) && (zf_fill(zf) == -1)) {
	    printf("zf_read: fill error\n");
	    return(EIO);
	}
	if (zf->zf_zstream.avail_in == 0) {		/* oops, unexpected EOF */
	    printf("zf_read: unexpected EOF\n");
	    if (zf->zf_zstream.avail_out == size)
		return(EIO);
	    break;
	}

	error = inflate(&zf->zf_zstream, Z_SYNC_FLUSH);	/* decompression pass */
	if (error == Z_STREAM_END) {			/* EOF, all done */
	    zf->zf_endseen = 1;
	    break;
	}
	if (error != Z_OK) {				/* argh, decompression error */
	    printf("inflate: %s\n", zf->zf_zstream.msg);
	    return(EIO);
	}
    }
    if (resid != NULL)
	*resid = zf->zf_zstream.avail_out;
    return(0);
}

static int
zf_rewind(struct open_file *f)
{
    struct z_file	*zf = (struct z_file *)f->f_fsdata;

    if (lseek(zf->zf_rawfd, zf->zf_dataoffset, SEEK_SET) == -1)
	return(-1);
    zf->zf_zstream.avail_in = 0;
    zf->zf_zstream.next_in = NULL;
    zf->zf_endseen = 0;
    (void)inflateReset(&zf->zf_zstream);

    return(0);
}

static off_t
zf_seek(struct open_file *f, off_t offset, int where)
{
    struct z_file	*zf = (struct z_file *)f->f_fsdata;
    off_t		target;
    char		discard[16];
    
    switch (where) {
    case SEEK_SET:
	target = offset;
	break;
    case SEEK_CUR:
	target = offset + zf->zf_zstream.total_out;
	break;
    default:
	errno = EINVAL;
	return(-1);
    }

    /* rewind if required */
    if (target < zf->zf_zstream.total_out && zf_rewind(f) != 0)
	return(-1);

    /* skip forwards if required */
    while (target > zf->zf_zstream.total_out) {
	errno = zf_read(f, discard, min(sizeof(discard),
	    target - zf->zf_zstream.total_out), NULL);
	if (errno)
	    return(-1);
    }
    /* This is where we are (be honest if we overshot) */
    return(zf->zf_zstream.total_out);
}


static int
zf_stat(struct open_file *f, struct stat *sb)
{
    struct z_file	*zf = (struct z_file *)f->f_fsdata;
    int			result;

    /* stat as normal, but indicate that size is unknown */
    if ((result = fstat(zf->zf_rawfd, sb)) == 0)
	sb->st_size = -1;
    return(result);
}



