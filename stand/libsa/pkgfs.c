/*- 
 * Copyright (c) 2007-2014, Juniper Networks, Inc.
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
#include <sys/stdint.h>
#include <string.h>
#include <zlib.h>

#ifdef PKGFS_DEBUG
#define	DBG(x)	printf x
#else
#define	DBG(x)
#endif

static int   pkg_open(const char *, struct open_file *);
static int   pkg_close(struct open_file *);
static int   pkg_read(struct open_file *, void *, size_t, size_t *);
static off_t pkg_seek(struct open_file *, off_t, int);
static int   pkg_stat(struct open_file *, struct stat *);
static int   pkg_readdir(struct open_file *, struct dirent *);

struct fs_ops pkgfs_fsops = {
	"pkg",
	pkg_open, 
	pkg_close, 
	pkg_read,
	null_write,
	pkg_seek,
	pkg_stat,
	pkg_readdir
};

#define PKG_BUFSIZE	512
#define	PKG_MAXCACHESZ	4096

#define	PKG_FILEEXT	".tgz"

/*
 * Layout of POSIX 'ustar' header.
 */
struct ustar_hdr {
	char	ut_name[100];
	char	ut_mode[8];
	char	ut_uid[8];
	char	ut_gid[8];
	char	ut_size[12];
	char	ut_mtime[12];
	char	ut_checksum[8];
	char	ut_typeflag[1];
	char	ut_linkname[100];
	char	ut_magic[6];		/* For POSIX: "ustar\0" */
	char	ut_version[2];		/* For POSIX: "00" */
	char	ut_uname[32];
	char	ut_gname[32];
	char	ut_rdevmajor[8];
	char	ut_rdevminor[8];
	union {
		struct {
			char	prefix[155];
		} posix;
		struct {
			char	atime[12];
			char	ctime[12];
			char	offset[12];
			char	longnames[4];
			char	unused[1];
			struct gnu_sparse {
				char	offset[12];
				char	numbytes[12];
			} sparse[4];
			char	isextended[1];
			char	realsize[12];
		} gnu;
	} u;
	u_char __padding[12];
};

struct package;

struct tarfile
{
	struct package *tf_pkg;
	struct tarfile *tf_next;
	struct ustar_hdr tf_hdr;
	off_t	tf_ofs;
	off_t	tf_size;
	off_t	tf_fp;
	size_t	tf_cachesz;
	void	*tf_cache;
};

struct package
{
	struct package *pkg_chain;
	int	pkg_fd;
	off_t	pkg_ofs;
	z_stream pkg_zs;
	struct tarfile *pkg_first;
	struct tarfile *pkg_last;
	u_char	pkg_buf[PKG_BUFSIZE];
};

static struct package *package = NULL;

static int new_package(int, struct package **);

void
pkgfs_cleanup(void)
{
	struct package *chain;
	struct tarfile *tf, *tfn;

	while (package != NULL) {
		inflateEnd(&package->pkg_zs);
		close(package->pkg_fd);

		tf = package->pkg_first;
		while (tf != NULL) {
			tfn = tf->tf_next;
			if (tf->tf_cachesz > 0)
				free(tf->tf_cache);
			free(tf);
			tf = tfn;
		}

		chain = package->pkg_chain;
		free(package);
		package = chain;
	}
}

int
pkgfs_init(const char *pkgname, struct fs_ops *proto)
{
	struct package *pkg;
	int error, fd;

	pkg = NULL;
	if (proto != &pkgfs_fsops)
		pkgfs_cleanup();

	exclusive_file_system = proto;

	fd = open(pkgname, O_RDONLY);

	exclusive_file_system = NULL;

	if (fd == -1)
		return (errno);

	error = new_package(fd, &pkg);
	if (error) {
		close(fd);
		return (error);
	}

	if (pkg == NULL)
		return (EDOOFUS);

	pkg->pkg_chain = package;
	package = pkg;
	exclusive_file_system = &pkgfs_fsops;
	return (0);
}

static int get_mode(struct tarfile *);
static int get_zipped(struct package *, void *, size_t);
static int new_package(int, struct package **);
static struct tarfile *scan_tarfile(struct package *, struct tarfile *);

static int
pkg_open(const char *fn, struct open_file *f)
{
	struct tarfile *tf;

	if (fn == NULL || f == NULL)
		return (EINVAL);

	if (package == NULL)
		return (ENXIO);

	/*
	 * We can only read from a package, so reject request to open
	 * for write-only or read-write.
	 */
	if (f->f_flags != F_READ)
		return (EPERM);

	/*
	 * Scan the file headers for the named file. We stop scanning
	 * at the first filename that has the .pkg extension. This is
	 * a package within a package. We assume we have all the files
	 * we need up-front and without having to dig within nested
	 * packages.
	 *
	 * Note that we preserve streaming properties as much as possible.
	 */
	while (*fn == '/')
		fn++;

	/*
	 * Allow opening of the root directory for use by readdir()
	 * to support listing files in the package.
	 */
	if (*fn == '\0') {
		f->f_fsdata = NULL;
		return (0);
	}

	tf = scan_tarfile(package, NULL);
	while (tf != NULL) {
		if (strcmp(fn, tf->tf_hdr.ut_name) == 0) {
			f->f_fsdata = tf;
			tf->tf_fp = 0;	/* Reset the file pointer. */
			return (0);
		}
		tf = scan_tarfile(package, tf);
	}
	return (errno);
}

static int
pkg_close(struct open_file *f)
{
	struct tarfile *tf;

	tf = (struct tarfile *)f->f_fsdata;
	if (tf == NULL)
		return (0);

	/*
	 * Free up the cache if we read all of the file.
	 */
	if (tf->tf_fp == tf->tf_size && tf->tf_cachesz > 0) {
		free(tf->tf_cache);
		tf->tf_cachesz = 0;
	}
	return (0);
}

static int
pkg_read(struct open_file *f, void *buf, size_t size, size_t *res)
{
	struct tarfile *tf;
	char *p;
	off_t fp;
	size_t sz;

	tf = (struct tarfile *)f->f_fsdata;
	if (tf == NULL) {
		if (res != NULL)
			*res = size;
		return (EBADF);
	}

	fp = tf->tf_fp;
	p = buf;
	sz = 0;
	while (size > 0) {
		sz = tf->tf_size - fp;
		if (fp < tf->tf_cachesz && tf->tf_cachesz < tf->tf_size)
			sz = tf->tf_cachesz - fp;
		if (size < sz)
			sz = size;
		if (sz == 0)
			break;

		if (fp < tf->tf_cachesz) {
			/* Satisfy the request from cache. */
			memcpy(p, tf->tf_cache + fp, sz);
			fp += sz;
			p += sz;
			size -= sz;
			continue;
		}

		if (get_zipped(tf->tf_pkg, p, sz) == -1) {
			sz = -1;
			break;
		}

		fp += sz;
		p += sz;
		size -= sz;

		if (tf->tf_cachesz != 0)
			continue;

		tf->tf_cachesz = (sz <= PKG_MAXCACHESZ) ? sz : PKG_MAXCACHESZ;
		tf->tf_cache = malloc(tf->tf_cachesz);
		if (tf->tf_cache != NULL)
			memcpy(tf->tf_cache, buf, tf->tf_cachesz);
		else
			tf->tf_cachesz = 0;
	}

	tf->tf_fp = fp;
	if (res != NULL)
		*res = size;
	return ((sz == -1) ? errno : 0);
}

static off_t
pkg_seek(struct open_file *f, off_t ofs, int whence)
{
	char buf[512];
	struct tarfile *tf;
	off_t delta;
	size_t sz, res;
	int error;

	tf = (struct tarfile *)f->f_fsdata;
	if (tf == NULL) {
		errno = EBADF;
		return (-1);
	}

	switch (whence) {
	case SEEK_SET:
		delta = ofs - tf->tf_fp;
		break;
	case SEEK_CUR:
		delta = ofs;
		break;
	case SEEK_END:
		delta = tf->tf_size - tf->tf_fp + ofs;
		break;
	default:
		errno = EINVAL;
		return (-1);
	}

	if (delta < 0) {
		DBG(("%s: negative file seek (%jd)\n", __func__,
		    (intmax_t)delta));
		errno = ESPIPE;
		return (-1);
	}

	while (delta > 0 && tf->tf_fp < tf->tf_size) {
		sz = (delta > sizeof(buf)) ? sizeof(buf) : delta;
		error = pkg_read(f, buf, sz, &res);
		if (error != 0) {
			errno = error;
			return (-1);
		}
		delta -= sz - res;
	}

	return (tf->tf_fp);
}

static int
pkg_stat(struct open_file *f, struct stat *sb)
{
	struct tarfile *tf;

	tf = (struct tarfile *)f->f_fsdata;
	if (tf == NULL)
		return (EBADF);
	memset(sb, 0, sizeof(*sb));
	sb->st_mode = get_mode(tf);
	sb->st_size = tf->tf_size;
	sb->st_blocks = (tf->tf_size + 511) / 512;
	return (0);
}

static int
pkg_readdir(struct open_file *f, struct dirent *d)
{
	struct tarfile *tf;

	tf = (struct tarfile *)f->f_fsdata;
	if (tf != NULL)
		return (EBADF);

	tf = scan_tarfile(package, NULL);
	if (tf == NULL)
		return (ENOENT);

	d->d_fileno = 0;
	d->d_reclen = sizeof(*d);
	d->d_type = DT_REG;
	memcpy(d->d_name, tf->tf_hdr.ut_name, sizeof(d->d_name));
	return (0);
}

/*
 * Low-level support functions.
 */

static int
get_byte(struct package *pkg, off_t *op)
{
	int c;

	if (pkg->pkg_zs.avail_in == 0) {
		c = read(pkg->pkg_fd, pkg->pkg_buf, PKG_BUFSIZE);
		if (c <= 0)
			return (-1);
		pkg->pkg_zs.avail_in = c;
		pkg->pkg_zs.next_in = pkg->pkg_buf;
	}

	c = *pkg->pkg_zs.next_in;
	pkg->pkg_zs.next_in++;
	pkg->pkg_zs.avail_in--;
	(*op)++;
	return (c);
}

static int
get_zipped(struct package *pkg, void *buf, size_t bufsz)
{
	int c;

	pkg->pkg_zs.next_out = buf;
	pkg->pkg_zs.avail_out = bufsz;

	while (pkg->pkg_zs.avail_out) {
		if (pkg->pkg_zs.avail_in == 0) {
			c = read(pkg->pkg_fd, pkg->pkg_buf, PKG_BUFSIZE);
			if (c <= 0) {
				errno = EIO;
				return (-1);
			}
			pkg->pkg_zs.avail_in = c;
			pkg->pkg_zs.next_in = pkg->pkg_buf;
		}

		c = inflate(&pkg->pkg_zs, Z_SYNC_FLUSH);
		if (c != Z_OK && c != Z_STREAM_END) {
			errno = EIO;
			return (-1);
		}
	}

	pkg->pkg_ofs += bufsz;
	return (0);
}

static int
cache_data(struct tarfile *tf)
{
	struct package *pkg;
	size_t sz;

	if (tf == NULL) {
		DBG(("%s: no file to cache data for?\n", __func__));
		errno = EINVAL;
		return (-1);
	}

	pkg = tf->tf_pkg;
	if (pkg == NULL) {
		DBG(("%s: no package associated with file?\n", __func__));
		errno = EINVAL;
		return (-1);
	}

	if (tf->tf_ofs != pkg->pkg_ofs) {
		DBG(("%s: caching after partial read of file %s?\n",
		    __func__, tf->tf_hdr.ut_name));
		errno = EINVAL;
		return (-1);
	}

	/* We don't cache everything... */
	if (tf->tf_size > PKG_MAXCACHESZ) {
		errno = ENOMEM;
		return (-1);
	}

	/* All files are padded to a multiple of 512 bytes. */
	sz = (tf->tf_size + 0x1ff) & ~0x1ff;

	tf->tf_cache = malloc(sz);
	if (tf->tf_cache == NULL) {
		DBG(("%s: could not allocate %d bytes\n", __func__, (int)sz));
		errno = ENOMEM;
		return (-1);
	}

	tf->tf_cachesz = sz;
	return (get_zipped(pkg, tf->tf_cache, sz));
}

/*
 * Note that this implementation does not (and should not!) obey
 * locale settings; you cannot simply substitute strtol here, since
 * it does obey locale.
 */
static off_t
pkg_atol8(const char *p, unsigned char_cnt)
{
        int64_t l, limit, last_digit_limit;
        int digit, sign, base;

        base = 8;
        limit = INT64_MAX / base;
        last_digit_limit = INT64_MAX % base;

        while (*p == ' ' || *p == '\t')
                p++;
        if (*p == '-') {
                sign = -1;
                p++;
        } else
                sign = 1;

        l = 0;
        digit = *p - '0';
        while (digit >= 0 && digit < base  && char_cnt-- > 0) {
                if (l>limit || (l == limit && digit > last_digit_limit)) {
                        l = UINT64_MAX; /* Truncate on overflow. */
                        break;
                }
                l = (l * base) + digit;
                digit = *++p - '0';
        }
        return (sign < 0) ? -l : l;
}

/*
 * Parse a base-256 integer.  This is just a straight signed binary
 * value in big-endian order, except that the high-order bit is
 * ignored.  Remember that "int64_t" may or may not be exactly 64
 * bits; the implementation here tries to avoid making any assumptions
 * about the actual size of an int64_t.  It does assume we're using
 * twos-complement arithmetic, though.
 */
static int64_t
pkg_atol256(const char *_p, unsigned char_cnt)
{
        int64_t l, upper_limit, lower_limit;
        const unsigned char *p = (const unsigned char *)_p;

        upper_limit = INT64_MAX / 256;
        lower_limit = INT64_MIN / 256;

        /* Pad with 1 or 0 bits, depending on sign. */
        if ((0x40 & *p) == 0x40)
                l = (int64_t)-1;
        else
                l = 0;
        l = (l << 6) | (0x3f & *p++);
        while (--char_cnt > 0) {
                if (l > upper_limit) {
                        l = INT64_MAX; /* Truncate on overflow */
                        break;
                } else if (l < lower_limit) {
                        l = INT64_MIN;
                        break;
                }
                l = (l << 8) | (0xff & (int64_t)*p++);
        }
        return (l);
}

static off_t
pkg_atol(const char *p, unsigned char_cnt)
{
	/*
	 * Technically, GNU pkg considers a field to be in base-256
	 * only if the first byte is 0xff or 0x80.
	 */
	if (*p & 0x80)
		return (pkg_atol256(p, char_cnt));
	return (pkg_atol8(p, char_cnt));
}

static int
get_mode(struct tarfile *tf)
{
	return (pkg_atol(tf->tf_hdr.ut_mode, sizeof(tf->tf_hdr.ut_mode)));
}

/* GZip flag byte */
#define ASCII_FLAG	0x01 /* bit 0 set: file probably ascii text */
#define HEAD_CRC	0x02 /* bit 1 set: header CRC present */
#define EXTRA_FIELD	0x04 /* bit 2 set: extra field present */
#define ORIG_NAME	0x08 /* bit 3 set: original file name present */
#define COMMENT		0x10 /* bit 4 set: file comment present */
#define RESERVED	0xE0 /* bits 5..7: reserved */

static int
new_package(int fd, struct package **pp)
{
	struct package *pkg;
	off_t ofs;
	int flags, i, error;

	pkg = malloc(sizeof(*pkg));
	if (pkg == NULL)
		return (ENOMEM);

	bzero(pkg, sizeof(*pkg));
	pkg->pkg_fd = fd;

	/*
	 * Parse the header.
	 */
	error = EFTYPE;
	ofs = 0;

	/* Check megic. */
	if (get_byte(pkg, &ofs) != 0x1f || get_byte(pkg, &ofs) != 0x8b)
		goto fail;
	/* Check method. */
	if (get_byte(pkg, &ofs) != Z_DEFLATED)
		goto fail;
	/* Check flags. */
	flags = get_byte(pkg, &ofs);
	if (flags & RESERVED)
		goto fail;

	/* Skip time, xflags and OS code. */
	for (i = 0; i < 6; i++) {
		if (get_byte(pkg, &ofs) == -1)
			goto fail;
	}

	/* Skip extra field. */
	if (flags & EXTRA_FIELD) {
		i = (get_byte(pkg, &ofs) & 0xff) |
		    ((get_byte(pkg, &ofs) << 8) & 0xff);
		while (i-- > 0) {
			if (get_byte(pkg, &ofs) == -1)
				goto fail;
		}
	}

	/* Skip original file name. */
	if (flags & ORIG_NAME) {
		do {
			i = get_byte(pkg, &ofs);
		} while (i != 0 && i != -1);
		if (i == -1)
			goto fail;
	}

	/* Print the comment if it's there. */
	if (flags & COMMENT) {
		while (1) {
			i = get_byte(pkg, &ofs);
			if (i == -1)
				goto fail;
			if (i == 0)
				break;
			putchar(i);
		}
	}

	/* Skip the CRC. */
	if (flags & HEAD_CRC) {
		if (get_byte(pkg, &ofs) == -1)
			goto fail;
		if (get_byte(pkg, &ofs) == -1)
			goto fail;
	}

	/*
	 * Done parsing the ZIP header. Spkgt the inflation engine.
	 */
	error = inflateInit2(&pkg->pkg_zs, -15);
	if (error != Z_OK)
		goto fail;

	*pp = pkg;
	return (0);

 fail:
	free(pkg);
	return (error);
}

static struct tarfile *
scan_tarfile(struct package *pkg, struct tarfile *last)
{
	char buf[512];
	struct tarfile *cur;
	off_t ofs;
	size_t sz;

	cur = (last != NULL) ? last->tf_next : pkg->pkg_first;
	if (cur == NULL) {
		ofs = (last != NULL) ? last->tf_ofs + last->tf_size :
		    pkg->pkg_ofs;
		ofs = (ofs + 0x1ff) & ~0x1ff;

		/* Check if we've reached EOF. */
		if (ofs < pkg->pkg_ofs) {
			errno = ENOSPC;
			return (NULL);
		}

		if (ofs != pkg->pkg_ofs) {
			if (last != NULL && pkg->pkg_ofs == last->tf_ofs) {
				if (cache_data(last) == -1)
					return (NULL);
			} else {
				sz = ofs - pkg->pkg_ofs;
				while (sz != 0) {
					if (sz > sizeof(buf))
						sz = sizeof(buf);
					if (get_zipped(pkg, buf, sz) == -1)
						return (NULL);
					sz = ofs - pkg->pkg_ofs;
				}
			}
		}

		cur = malloc(sizeof(*cur));
		if (cur == NULL)
			return (NULL);
		memset(cur, 0, sizeof(*cur));
		cur->tf_pkg = pkg;

		while (1) {
			if (get_zipped(pkg, &cur->tf_hdr,
			    sizeof(cur->tf_hdr)) == -1) {
				free(cur);
				return (NULL);
			}

			/*
			 * There are always 2 empty blocks appended to
			 * a PKG. It marks the end of the archive.
			 */
			if (strncmp(cur->tf_hdr.ut_magic, "ustar", 5) != 0) {
				free(cur);
				errno = ENOSPC;
				return (NULL);
			}

			cur->tf_ofs = pkg->pkg_ofs;
			cur->tf_size = pkg_atol(cur->tf_hdr.ut_size,
			    sizeof(cur->tf_hdr.ut_size));

			if (cur->tf_hdr.ut_name[0] != '+')
				break;

			/*
			 * Skip package meta-files.
			 */
			ofs = cur->tf_ofs + cur->tf_size;
			ofs = (ofs + 0x1ff) & ~0x1ff;
			while (pkg->pkg_ofs < ofs) {
				if (get_zipped(pkg, buf, sizeof(buf)) == -1) {
					free(cur);
					return (NULL);
				}
			}
		}

		if (last != NULL)
			last->tf_next = cur;
		else
			pkg->pkg_first = cur;
		pkg->pkg_last = cur;
	}

	return (cur);
}
