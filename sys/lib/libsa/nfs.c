/*	$OpenBSD: nfs.c,v 1.15 2017/05/08 20:13:46 patrick Exp $	*/
/*	$NetBSD: nfs.c,v 1.19 1996/10/13 02:29:04 christos Exp $	*/

/*-
 *  Copyright (c) 1993 John Brezak
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR `AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <netinet/in.h>

#include "rpcv2.h"
#include "nfsv2.h"

#include "stand.h"
#include "saerrno.h"
#include "net.h"
#include "netif.h"
#include "nfs.h"
#include "rpc.h"

/* Define our own NFS attributes without NQNFS stuff. */
struct nfsv2_fattrs {
	u_int32_t	fa_type;
	u_int32_t	fa_mode;
	u_int32_t	fa_nlink;
	u_int32_t	fa_uid;
	u_int32_t	fa_gid;
	u_int32_t	fa_size;
	u_int32_t	fa_blocksize;
	u_int32_t	fa_rdev;
	u_int32_t	fa_blocks;
	u_int32_t	fa_fsid;
	u_int32_t	fa_fileid;
	struct nfsv2_time fa_atime;
	struct nfsv2_time fa_mtime;
	struct nfsv2_time fa_ctime;
};


struct nfs_read_args {
	u_char	fh[NFS_FHSIZE];
	u_int32_t	off;
	u_int32_t	len;
	u_int32_t	xxx;			/* XXX what's this for? */
};

/* Data part of nfs rpc reply (also the largest thing we receive) */
#define NFSREAD_SIZE 1024
struct nfs_read_repl {
	u_int32_t	errno;
	struct	nfsv2_fattrs fa;
	u_int32_t	count;
	u_char	data[NFSREAD_SIZE];
};

struct nfs_readlnk_repl {
	u_int32_t	errno;
	u_int32_t	len;
	char	path[NFS_MAXPATHLEN];
};

struct nfs_iodesc {
	struct	iodesc	*iodesc;
	off_t	off;
	u_char	fh[NFS_FHSIZE];
	struct nfsv2_fattrs fa;	/* all in network order */
};

struct nfs_iodesc nfs_root_node;


/*
 * Fetch the root file handle (call mount daemon)
 * On error, return non-zero and set errno.
 */
static int
nfs_getrootfh(struct iodesc *d, const char *path, u_char *fhp)
{
	int len;
	struct args {
		u_int32_t	len;
		char	path[FNAME_SIZE];
	} *args;
	struct repl {
		u_int32_t	errno;
		u_char	fh[NFS_FHSIZE];
	} *repl;
	struct {
		u_int32_t	h[RPC_HEADER_WORDS];
		struct args d;
	} sdata;
	struct {
		u_int32_t	h[RPC_HEADER_WORDS];
		struct repl d;
	} rdata;
	ssize_t cc;

#ifdef NFS_DEBUG
	if (debug)
		printf("nfs_getrootfh: %s\n", path);
#endif

	args = &sdata.d;
	repl = &rdata.d;

	bzero(args, sizeof(*args));
	len = strlen(path);
	if (len > sizeof(args->path))
		len = sizeof(args->path);
	args->len = htonl(len);
	bcopy(path, args->path, len);
	len = 4 + roundup(len, 4);

	cc = rpc_call(d, RPCPROG_MNT, RPCMNT_VER1, RPCMNT_MOUNT,
	    args, len, repl, sizeof(*repl));
	if (cc == -1) {
		/* errno was set by rpc_call */
		return (-1);
	}
	if (cc < 4) {
		errno = EBADRPC;
		return (-1);
	}
	if (repl->errno) {
		errno = ntohl(repl->errno);
		return (-1);
	}
	bcopy(repl->fh, fhp, sizeof(repl->fh));
	return (0);
}

/*
 * Lookup a file.  Store handle and attributes.
 * Return zero or error number.
 */
static int
nfs_lookupfh(struct nfs_iodesc *d, char *name, struct nfs_iodesc *newfd)
{
	int len, rlen;
	struct args {
		u_char	fh[NFS_FHSIZE];
		u_int32_t	len;
		char	name[FNAME_SIZE];
	} *args;
	struct repl {
		u_int32_t	errno;
		u_char	fh[NFS_FHSIZE];
		struct	nfsv2_fattrs fa;
	} *repl;
	struct {
		u_int32_t	h[RPC_HEADER_WORDS];
		struct args d;
	} sdata;
	struct {
		u_int32_t	h[RPC_HEADER_WORDS];
		struct repl d;
	} rdata;
	ssize_t cc;

#ifdef NFS_DEBUG
	if (debug)
		printf("lookupfh: called\n");
#endif

	args = &sdata.d;
	repl = &rdata.d;

	bzero(args, sizeof(*args));
	bcopy(d->fh, args->fh, sizeof(args->fh));
	len = strlen(name);
	if (len > sizeof(args->name))
		len = sizeof(args->name);
	bcopy(name, args->name, len);
	args->len = htonl(len);
	len = 4 + roundup(len, 4);
	len += NFS_FHSIZE;

	rlen = sizeof(*repl);

	cc = rpc_call(d->iodesc, NFS_PROG, NFS_VER2, NFSPROC_LOOKUP,
	    args, len, repl, rlen);
	if (cc == -1)
		return (errno);		/* XXX - from rpc_call */
	if (cc < 4)
		return (EIO);
	if (repl->errno) {
		/* saerrno.h now matches NFS error numbers. */
		return (ntohl(repl->errno));
	}
	bcopy( repl->fh, &newfd->fh, sizeof(newfd->fh));
	bcopy(&repl->fa, &newfd->fa, sizeof(newfd->fa));
	return (0);
}

/*
 * Get the destination of a symbolic link.
 */
static int
nfs_readlink(struct nfs_iodesc *d, char *buf)
{
	struct {
		u_int32_t	h[RPC_HEADER_WORDS];
		u_char fh[NFS_FHSIZE];
	} sdata;
	struct {
		u_int32_t	h[RPC_HEADER_WORDS];
		struct nfs_readlnk_repl d;
	} rdata;
	ssize_t cc;

#ifdef NFS_DEBUG
	if (debug)
		printf("readlink: called\n");
#endif

	bcopy(d->fh, sdata.fh, NFS_FHSIZE);
	cc = rpc_call(d->iodesc, NFS_PROG, NFS_VER2, NFSPROC_READLINK,
	    sdata.fh, NFS_FHSIZE,
	    &rdata.d, sizeof(rdata.d));
	if (cc == -1)
		return (errno);

	if (cc < 4)
		return (EIO);

	if (rdata.d.errno)
		return (ntohl(rdata.d.errno));

	rdata.d.len = ntohl(rdata.d.len);
	if (rdata.d.len > NFS_MAXPATHLEN)
		return (ENAMETOOLONG);

	bcopy(rdata.d.path, buf, rdata.d.len);
	buf[rdata.d.len] = 0;
	return (0);
}

/*
 * Read data from a file.
 * Return transfer count or -1 (and set errno)
 */
static ssize_t
nfs_readdata(struct nfs_iodesc *d, off_t off, void *addr, size_t len)
{
	struct nfs_read_args *args;
	struct nfs_read_repl *repl;
	struct {
		u_int32_t	h[RPC_HEADER_WORDS];
		struct nfs_read_args d;
	} sdata;
	struct {
		u_int32_t	h[RPC_HEADER_WORDS];
		struct nfs_read_repl d;
	} rdata;
	ssize_t cc;
	long x;
	int hlen, rlen;

	args = &sdata.d;
	repl = &rdata.d;

	bcopy(d->fh, args->fh, NFS_FHSIZE);
	args->off = htonl((u_int32_t)off);
	if (len > NFSREAD_SIZE)
		len = NFSREAD_SIZE;
	args->len = htonl((u_int32_t)len);
	args->xxx = htonl((u_int32_t)0);
	hlen = sizeof(*repl) - NFSREAD_SIZE;

	cc = rpc_call(d->iodesc, NFS_PROG, NFS_VER2, NFSPROC_READ,
	    args, sizeof(*args),
	    repl, sizeof(*repl));
	if (cc == -1) {
		/* errno was already set by rpc_call */
		return (-1);
	}
	if (cc < hlen) {
		errno = EBADRPC;
		return (-1);
	}
	if (repl->errno) {
		errno = ntohl(repl->errno);
		return (-1);
	}
	rlen = cc - hlen;
	x = ntohl(repl->count);
	if (rlen < x) {
		printf("nfsread: short packet, %d < %ld\n", rlen, x);
		errno = EBADRPC;
		return(-1);
	}
	bcopy(repl->data, addr, x);
	return (x);
}

/*
 * nfs_mount - mount this nfs filesystem to a host
 * On error, return non-zero and set errno.
 */
int
nfs_mount(int sock, struct in_addr ip, const char *path)
{
	struct iodesc *desc;
	struct nfsv2_fattrs *fa;

	if (!(desc = socktodesc(sock))) {
		errno = EINVAL;
		return(-1);
	}

	/* Bind to a reserved port. */
	desc->myport = htons(--rpc_port);
	desc->destip = ip;
	if (nfs_getrootfh(desc, path, nfs_root_node.fh))
		return (-1);
	nfs_root_node.iodesc = desc;
	/* Fake up attributes for the root dir. */
	fa = &nfs_root_node.fa;
	fa->fa_type  = htonl(NFDIR);
	fa->fa_mode  = htonl(0755);
	fa->fa_nlink = htonl(2);

#ifdef NFS_DEBUG
	if (debug)
		printf("nfs_mount: got fh for %s\n", path);
#endif

	return(0);
}

/*
 * Open a file.
 * return zero or error number
 */
int
nfs_open(char *path, struct open_file *f)
{
	struct nfs_iodesc *newfd, *currfd;
	char namebuf[NFS_MAXPATHLEN + 1], *cp, *ncp;
	char linkbuf[NFS_MAXPATHLEN + 1];
	int nlinks = 0, error = 0, c;

#ifdef NFS_DEBUG
	if (debug)
		printf("nfs_open: %s\n", path);
#endif
	if (nfs_root_node.iodesc == NULL) {
		printf("nfs_open: must mount first.\n");
		return (ENXIO);
	}

	currfd = &nfs_root_node;
	newfd = 0;

	cp = path;
	while (*cp) {
		/*
		 * Remove extra separators
		 */
		while (*cp == '/')
			cp++;

		if (*cp == '\0')
			break;
		/*
		 * Check that current node is a directory.
		 */
		if (currfd->fa.fa_type != htonl(NFDIR)) {
			error = ENOTDIR;
			goto out;
		}

		/* allocate file system specific data structure */
		newfd = alloc(sizeof(*newfd));
		newfd->iodesc = currfd->iodesc;
		newfd->off = 0;

		/*
		 * Get next component of path name.
		 */
		{
			int len = 0;

			ncp = cp;
			while ((c = *cp) != '\0' && c != '/') {
				if (++len > NFS_MAXNAMLEN) {
					error = ENOENT;
					goto out;
				}
				cp++;
			}
			*cp = '\0';
		}

		/* lookup a file handle */
		error = nfs_lookupfh(currfd, ncp, newfd);
		*cp = c;
		if (error)
			goto out;

		/*
		 * Check for symbolic link
		 */
		if (newfd->fa.fa_type == htonl(NFLNK)) {
			int link_len, len;

			error = nfs_readlink(newfd, linkbuf);
			if (error)
				goto out;

			link_len = strlen(linkbuf);
			len = strlen(cp);

			if (link_len + len > MAXPATHLEN ||
			    ++nlinks > MAXSYMLINKS) {
				error = ENOENT;
				goto out;
			}

			bcopy(cp, &namebuf[link_len], len + 1);
			bcopy(linkbuf, namebuf, link_len);

			/*
			 * If absolute pathname, restart at root.
			 * If relative pathname, restart at parent directory.
			 */
			cp = namebuf;
			if (*cp == '/') {
				if (currfd != &nfs_root_node)
					free(currfd, sizeof(*currfd));
				currfd = &nfs_root_node;
			}

			free(newfd, sizeof(*newfd));
			newfd = 0;

			continue;
		}

		if (currfd != &nfs_root_node)
			free(currfd, sizeof(*currfd));
		currfd = newfd;
		newfd = 0;
	}

	error = 0;

out:
	if (!error) {
		f->f_fsdata = (void *)currfd;
		return (0);
	}

#ifdef NFS_DEBUG
	if (debug)
		printf("nfs_open: %s lookupfh failed: %s\n",
		    path, strerror(error));
#endif
	if (currfd != &nfs_root_node)
		free(currfd, sizeof(*currfd));
	if (newfd)
		free(newfd, sizeof(*newfd));

	return (error);
}

int
nfs_close(struct open_file *f)
{
	struct nfs_iodesc *fp = (struct nfs_iodesc *)f->f_fsdata;

#ifdef NFS_DEBUG
	if (debug)
		printf("nfs_close: fp=%p\n", fp);
#endif

	if (fp)
		free(fp, sizeof(struct nfs_iodesc));
	f->f_fsdata = NULL;

	return (0);
}

/*
 * read a portion of a file
 */
int
nfs_read(struct open_file *f, void *buf, size_t size, size_t *resid)
{
	struct nfs_iodesc *fp = (struct nfs_iodesc *)f->f_fsdata;
	ssize_t cc;
	char *addr = buf;

#ifdef NFS_DEBUG
	if (debug)
		printf("nfs_read: size=%d off=%d\n", size, (int)fp->off);
#endif
	while ((int)size > 0) {
		twiddle();
		cc = nfs_readdata(fp, fp->off, (void *)addr, size);
		/* XXX maybe should retry on certain errors */
		if (cc == -1) {
#ifdef NFS_DEBUG
			if (debug)
				printf("nfs_read: read: %s", strerror(errno));
#endif
			return (errno);	/* XXX - from nfs_readdata */
		}
		if (cc == 0) {
#ifdef NFS_DEBUG
			if (debug)
				printf("nfs_read: hit EOF unexpectantly");
#endif
			goto ret;
		}
		fp->off += cc;
		addr += cc;
		size -= cc;
	}
ret:
	if (resid)
		*resid = size;

	return (0);
}

/*
 * Not implemented.
 */
int
nfs_write(struct open_file *f, void *buf, size_t size, size_t *resid)
{
	return (EROFS);
}

off_t
nfs_seek(struct open_file *f, off_t offset, int where)
{
	struct nfs_iodesc *d = (struct nfs_iodesc *)f->f_fsdata;
	u_int32_t size = ntohl(d->fa.fa_size);

	switch (where) {
	case SEEK_SET:
		d->off = offset;
		break;
	case SEEK_CUR:
		d->off += offset;
		break;
	case SEEK_END:
		d->off = size - offset;
		break;
	default:
		return (-1);
	}

	return (d->off);
}

/* NFNON=0, NFREG=1, NFDIR=2, NFBLK=3, NFCHR=4, NFLNK=5 */
const int nfs_stat_types[8] = {
	0, S_IFREG, S_IFDIR, S_IFBLK, S_IFCHR, S_IFLNK, 0
};

int
nfs_stat(struct open_file *f, struct stat *sb)
{
	struct nfs_iodesc *fp = (struct nfs_iodesc *)f->f_fsdata;
	u_int32_t ftype, mode;

	ftype = ntohl(fp->fa.fa_type);
	mode  = ntohl(fp->fa.fa_mode);
	mode |= nfs_stat_types[ftype & 7];

	sb->st_mode  = mode;
	sb->st_nlink = ntohl(fp->fa.fa_nlink);
	sb->st_uid   = ntohl(fp->fa.fa_uid);
	sb->st_gid   = ntohl(fp->fa.fa_gid);
	sb->st_size  = ntohl(fp->fa.fa_size);

	return (0);
}

/*
 * Not implemented.
 */
#ifndef NO_READDIR
int
nfs_readdir(struct open_file *f, char *name)
{
	return (EROFS);
}
#endif
