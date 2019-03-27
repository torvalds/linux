/*	$NetBSD: nfs.c,v 1.2 1998/01/24 12:43:09 drochner Exp $	*/

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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <string.h>
#include <stddef.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>

#include "rpcv2.h"
#include "nfsv2.h"

#include "stand.h"
#include "net.h"
#include "netif.h"
#include "rpc.h"

#define NFS_DEBUGxx

#define NFSREAD_MIN_SIZE 1024
#define NFSREAD_MAX_SIZE 16384

/* NFSv3 definitions */
#define	NFS_V3MAXFHSIZE		64
#define	NFS_VER3		3
#define	RPCMNT_VER3		3
#define	NFSPROCV3_LOOKUP	3
#define	NFSPROCV3_READLINK	5
#define	NFSPROCV3_READ		6
#define	NFSPROCV3_READDIR	16

typedef struct {
	uint32_t val[2];
} n_quad;

struct nfsv3_time {
	uint32_t nfs_sec;
	uint32_t nfs_nsec;
};

struct nfsv3_fattrs {
	uint32_t fa_type;
	uint32_t fa_mode;
	uint32_t fa_nlink;
	uint32_t fa_uid;
	uint32_t fa_gid;
	n_quad fa_size;
	n_quad fa_used;
	n_quad fa_rdev;
	n_quad fa_fsid;
	n_quad fa_fileid;
	struct nfsv3_time fa_atime;
	struct nfsv3_time fa_mtime;
	struct nfsv3_time fa_ctime;
};

/*
 * For NFSv3, the file handle is variable in size, so most fixed sized
 * structures for arguments won't work. For most cases, a structure
 * that starts with any fixed size section is followed by an array
 * that covers the maximum size required.
 */
struct nfsv3_readdir_repl {
	uint32_t errno;
	uint32_t ok;
	struct nfsv3_fattrs fa;
	uint32_t cookiev0;
	uint32_t cookiev1;
};

struct nfsv3_readdir_entry {
	uint32_t follows;
	uint32_t fid0;
	uint32_t fid1;
	uint32_t len;
	uint32_t nameplus[0];
};

struct nfs_iodesc {
	struct iodesc *iodesc;
	off_t off;
	uint32_t fhsize;
	u_char fh[NFS_V3MAXFHSIZE];
	struct nfsv3_fattrs fa;	/* all in network order */
	uint64_t cookie;
};

/*
 * XXX interactions with tftp? See nfswrapper.c for a confusing
 *     issue.
 */
int		nfs_open(const char *path, struct open_file *f);
static int	nfs_close(struct open_file *f);
static int	nfs_read(struct open_file *f, void *buf, size_t size, size_t *resid);
static off_t	nfs_seek(struct open_file *f, off_t offset, int where);
static int	nfs_stat(struct open_file *f, struct stat *sb);
static int	nfs_readdir(struct open_file *f, struct dirent *d);

struct	nfs_iodesc nfs_root_node;

struct fs_ops nfs_fsops = {
	"nfs",
	nfs_open,
	nfs_close,
	nfs_read,
	null_write,
	nfs_seek,
	nfs_stat,
	nfs_readdir
};

static int nfs_read_size = NFSREAD_MIN_SIZE;

/*
 * Improve boot performance over NFS
 */
static void
set_nfs_read_size(void)
{
	char *env, *end;
	char buf[10];

	if ((env = getenv("nfs.read_size")) != NULL) {
		errno = 0;
		nfs_read_size = (int)strtol(env, &end, 0);
		if (errno != 0 || *env == '\0' || *end != '\0') {
			printf("%s: bad value: \"%s\", defaulting to %d\n",
			    "nfs.read_size", env, NFSREAD_MIN_SIZE);
			nfs_read_size = NFSREAD_MIN_SIZE;
		}
	}
	if (nfs_read_size < NFSREAD_MIN_SIZE) {
		printf("%s: bad value: \"%d\", defaulting to %d\n",
		    "nfs.read_size", nfs_read_size, NFSREAD_MIN_SIZE);
		nfs_read_size = NFSREAD_MIN_SIZE;
	}
	if (nfs_read_size > NFSREAD_MAX_SIZE) {
		printf("%s: bad value: \"%d\", defaulting to %d\n",
		    "nfs.read_size", nfs_read_size, NFSREAD_MIN_SIZE);
		nfs_read_size = NFSREAD_MAX_SIZE;
	}
	snprintf(buf, sizeof (buf), "%d", nfs_read_size);
	setenv("nfs.read_size", buf, 1);
}

/*
 * Fetch the root file handle (call mount daemon)
 * Return zero or error number.
 */
int
nfs_getrootfh(struct iodesc *d, char *path, uint32_t *fhlenp, u_char *fhp)
{
	void *pkt = NULL;
	int len;
	struct args {
		uint32_t len;
		char path[FNAME_SIZE];
	} *args;
	struct repl {
		uint32_t errno;
		uint32_t fhsize;
		u_char fh[NFS_V3MAXFHSIZE];
		uint32_t authcnt;
		uint32_t auth[7];
	} *repl;
	struct {
		uint32_t h[RPC_HEADER_WORDS];
		struct args d;
	} sdata;
	size_t cc;

#ifdef NFS_DEBUG
	if (debug)
		printf("nfs_getrootfh: %s\n", path);
#endif

	args = &sdata.d;

	bzero(args, sizeof(*args));
	len = strlen(path);
	if (len > sizeof(args->path))
		len = sizeof(args->path);
	args->len = htonl(len);
	bcopy(path, args->path, len);
	len = sizeof(uint32_t) + roundup(len, sizeof(uint32_t));

	cc = rpc_call(d, RPCPROG_MNT, RPCMNT_VER3, RPCMNT_MOUNT,
	    args, len, (void **)&repl, &pkt);
	if (cc == -1) {
		free(pkt);
		/* errno was set by rpc_call */
		return (errno);
	}
	if (cc < 2 * sizeof (uint32_t)) {
		free(pkt);
		return (EBADRPC);
	}
	if (repl->errno != 0) {
		free(pkt);
		return (ntohl(repl->errno));
	}
	*fhlenp = ntohl(repl->fhsize);
	bcopy(repl->fh, fhp, *fhlenp);

	set_nfs_read_size();
	free(pkt);
	return (0);
}

/*
 * Lookup a file.  Store handle and attributes.
 * Return zero or error number.
 */
int
nfs_lookupfh(struct nfs_iodesc *d, const char *name, struct nfs_iodesc *newfd)
{
	void *pkt = NULL;
	int len, pos;
	struct args {
		uint32_t fhsize;
		uint32_t fhplusname[1 +
		    (NFS_V3MAXFHSIZE + FNAME_SIZE) / sizeof(uint32_t)];
	} *args;
	struct repl {
		uint32_t errno;
		uint32_t fhsize;
		uint32_t fhplusattr[(NFS_V3MAXFHSIZE +
		    2 * (sizeof(uint32_t) +
		    sizeof(struct nfsv3_fattrs))) / sizeof(uint32_t)];
	} *repl;
	struct {
		uint32_t h[RPC_HEADER_WORDS];
		struct args d;
	} sdata;
	ssize_t cc;

#ifdef NFS_DEBUG
	if (debug)
		printf("lookupfh: called\n");
#endif

	args = &sdata.d;

	bzero(args, sizeof(*args));
	args->fhsize = htonl(d->fhsize);
	bcopy(d->fh, args->fhplusname, d->fhsize);
	len = strlen(name);
	if (len > FNAME_SIZE)
		len = FNAME_SIZE;
	pos = roundup(d->fhsize, sizeof(uint32_t)) / sizeof(uint32_t);
	args->fhplusname[pos++] = htonl(len);
	bcopy(name, &args->fhplusname[pos], len);
	len = sizeof(uint32_t) + pos * sizeof(uint32_t) +
	    roundup(len, sizeof(uint32_t));

	cc = rpc_call(d->iodesc, NFS_PROG, NFS_VER3, NFSPROCV3_LOOKUP,
	    args, len, (void **)&repl, &pkt);
	if (cc == -1) {
		free(pkt);
		return (errno);		/* XXX - from rpc_call */
	}
	if (cc < 2 * sizeof(uint32_t)) {
		free(pkt);
		return (EIO);
	}
	if (repl->errno != 0) {
		free(pkt);
		/* saerrno.h now matches NFS error numbers. */
		return (ntohl(repl->errno));
	}
	newfd->fhsize = ntohl(repl->fhsize);
	bcopy(repl->fhplusattr, &newfd->fh, newfd->fhsize);
	pos = roundup(newfd->fhsize, sizeof(uint32_t)) / sizeof(uint32_t);
	if (repl->fhplusattr[pos++] == 0) {
		free(pkt);
		return (EIO);
	}
	bcopy(&repl->fhplusattr[pos], &newfd->fa, sizeof(newfd->fa));
	free(pkt);
	return (0);
}

#ifndef NFS_NOSYMLINK
/*
 * Get the destination of a symbolic link.
 */
int
nfs_readlink(struct nfs_iodesc *d, char *buf)
{
	void *pkt = NULL;
	struct args {
		uint32_t fhsize;
		u_char fh[NFS_V3MAXFHSIZE];
	} *args;
	struct repl {
		uint32_t errno;
		uint32_t ok;
		struct nfsv3_fattrs fa;
		uint32_t len;
		u_char path[NFS_MAXPATHLEN];
	} *repl;
	struct {
		uint32_t h[RPC_HEADER_WORDS];
		struct args d;
	} sdata;
	ssize_t cc;
	int rc = 0;

#ifdef NFS_DEBUG
	if (debug)
		printf("readlink: called\n");
#endif

	args = &sdata.d;

	bzero(args, sizeof(*args));
	args->fhsize = htonl(d->fhsize);
	bcopy(d->fh, args->fh, d->fhsize);
	cc = rpc_call(d->iodesc, NFS_PROG, NFS_VER3, NFSPROCV3_READLINK,
	    args, sizeof(uint32_t) + roundup(d->fhsize, sizeof(uint32_t)),
	    (void **)&repl, &pkt);
	if (cc == -1)
		return (errno);

	if (cc < 2 * sizeof(uint32_t)) {
		rc = EIO;
		goto done;
	}

	if (repl->errno != 0) {
		rc = ntohl(repl->errno);
		goto done;
	}

	if (repl->ok == 0) {
		rc = EIO;
		goto done;
	}

	repl->len = ntohl(repl->len);
	if (repl->len > NFS_MAXPATHLEN) {
		rc = ENAMETOOLONG;
		goto done;
	}

	bcopy(repl->path, buf, repl->len);
	buf[repl->len] = 0;
done:
	free(pkt);
	return (rc);
}
#endif

/*
 * Read data from a file.
 * Return transfer count or -1 (and set errno)
 */
ssize_t
nfs_readdata(struct nfs_iodesc *d, off_t off, void *addr, size_t len)
{
	void *pkt = NULL;
	struct args {
		uint32_t fhsize;
		uint32_t fhoffcnt[NFS_V3MAXFHSIZE / sizeof(uint32_t) + 3];
	} *args;
	struct repl {
		uint32_t errno;
		uint32_t ok;
		struct nfsv3_fattrs fa;
		uint32_t count;
		uint32_t eof;
		uint32_t len;
		u_char data[NFSREAD_MAX_SIZE];
	} *repl;
	struct {
		uint32_t h[RPC_HEADER_WORDS];
		struct args d;
	} sdata;
	size_t cc;
	long x;
	int hlen, rlen, pos;

	args = &sdata.d;

	bzero(args, sizeof(*args));
	args->fhsize = htonl(d->fhsize);
	bcopy(d->fh, args->fhoffcnt, d->fhsize);
	pos = roundup(d->fhsize, sizeof(uint32_t)) / sizeof(uint32_t);
	args->fhoffcnt[pos++] = 0;
	args->fhoffcnt[pos++] = htonl((uint32_t)off);
	if (len > nfs_read_size)
		len = nfs_read_size;
	args->fhoffcnt[pos] = htonl((uint32_t)len);
	hlen = offsetof(struct repl, data[0]);

	cc = rpc_call(d->iodesc, NFS_PROG, NFS_VER3, NFSPROCV3_READ,
	    args, 4 * sizeof(uint32_t) + roundup(d->fhsize, sizeof(uint32_t)),
	    (void **)&repl, &pkt);
	if (cc == -1) {
		/* errno was already set by rpc_call */
		return (-1);
	}
	if (cc < hlen) {
		errno = EBADRPC;
		free(pkt);
		return (-1);
	}
	if (repl->errno != 0) {
		errno = ntohl(repl->errno);
		free(pkt);
		return (-1);
	}
	rlen = cc - hlen;
	x = ntohl(repl->count);
	if (rlen < x) {
		printf("nfsread: short packet, %d < %ld\n", rlen, x);
		errno = EBADRPC;
		free(pkt);
		return (-1);
	}
	bcopy(repl->data, addr, x);
	free(pkt);
	return (x);
}

/*
 * Open a file.
 * return zero or error number
 */
int
nfs_open(const char *upath, struct open_file *f)
{
	struct iodesc *desc;
	struct nfs_iodesc *currfd = NULL;
	char buf[2 * NFS_V3MAXFHSIZE + 3];
	u_char *fh;
	char *cp;
	int i;
#ifndef NFS_NOSYMLINK
	struct nfs_iodesc *newfd = NULL;
	char *ncp;
	int c;
	char namebuf[NFS_MAXPATHLEN + 1];
	char linkbuf[NFS_MAXPATHLEN + 1];
	int nlinks = 0;
#endif
	int error;
	char *path = NULL;

	if (netproto != NET_NFS)
		return (EINVAL);

#ifdef NFS_DEBUG
 	if (debug)
 	    printf("nfs_open: %s (rootpath=%s)\n", upath, rootpath);
#endif
	if (!rootpath[0]) {
		printf("no rootpath, no nfs\n");
		return (ENXIO);
	}

	if (f->f_dev->dv_type != DEVT_NET)
		return (EINVAL);

	if (!(desc = socktodesc(*(int *)(f->f_devdata))))
		return (EINVAL);

	/* Bind to a reserved port. */
	desc->myport = htons(--rpc_port);
	desc->destip = rootip;
	if ((error = nfs_getrootfh(desc, rootpath, &nfs_root_node.fhsize,
	    nfs_root_node.fh)))
		return (error);
	nfs_root_node.fa.fa_type  = htonl(NFDIR);
	nfs_root_node.fa.fa_mode  = htonl(0755);
	nfs_root_node.fa.fa_nlink = htonl(2);
	nfs_root_node.iodesc = desc;

	fh = &nfs_root_node.fh[0];
	buf[0] = 'X';
	cp = &buf[1];
	for (i = 0; i < nfs_root_node.fhsize; i++, cp += 2)
		sprintf(cp, "%02x", fh[i]);
	sprintf(cp, "X");
	setenv("boot.nfsroot.server", inet_ntoa(rootip), 1);
	setenv("boot.nfsroot.path", rootpath, 1);
	setenv("boot.nfsroot.nfshandle", buf, 1);
	sprintf(buf, "%d", nfs_root_node.fhsize);
	setenv("boot.nfsroot.nfshandlelen", buf, 1);

	/* Allocate file system specific data structure */
	currfd = malloc(sizeof(*newfd));
	if (currfd == NULL) {
		error = ENOMEM;
		goto out;
	}
#ifndef NFS_NOSYMLINK
	bcopy(&nfs_root_node, currfd, sizeof(*currfd));
	newfd = NULL;

	cp = path = strdup(upath);
	if (path == NULL) {
		error = ENOMEM;
		goto out;
	}
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
		newfd = malloc(sizeof(*newfd));
		if (newfd == NULL) {
			error = ENOMEM;
			goto out;
		}
		newfd->iodesc = currfd->iodesc;

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

			if (link_len + len > MAXPATHLEN
			    || ++nlinks > MAXSYMLINKS) {
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
			if (*cp == '/')
				bcopy(&nfs_root_node, currfd, sizeof(*currfd));

			free(newfd);
			newfd = NULL;

			continue;
		}

		free(currfd);
		currfd = newfd;
		newfd = NULL;
	}

	error = 0;

out:
	free(newfd);
	free(path);
#else
	currfd->iodesc = desc;

	error = nfs_lookupfh(&nfs_root_node, upath, currfd);
#endif
	if (!error) {
		currfd->off = 0;
		currfd->cookie = 0;
		f->f_fsdata = (void *)currfd;
		return (0);
	}

#ifdef NFS_DEBUG
	if (debug)
		printf("nfs_open: %s lookupfh failed: %s\n",
		    path, strerror(error));
#endif
	free(currfd);

	return (error);
}

int
nfs_close(struct open_file *f)
{
	struct nfs_iodesc *fp = (struct nfs_iodesc *)f->f_fsdata;

#ifdef NFS_DEBUG
	if (debug)
		printf("nfs_close: fp=0x%lx\n", (u_long)fp);
#endif

	free(fp);
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
		printf("nfs_read: size=%lu off=%d\n", (u_long)size,
		       (int)fp->off);
#endif
	while ((int)size > 0) {
		twiddle(16);
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

off_t
nfs_seek(struct open_file *f, off_t offset, int where)
{
	struct nfs_iodesc *d = (struct nfs_iodesc *)f->f_fsdata;
	uint32_t size = ntohl(d->fa.fa_size.val[1]);

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
		errno = EINVAL;
		return (-1);
	}

	return (d->off);
}

/* NFNON=0, NFREG=1, NFDIR=2, NFBLK=3, NFCHR=4, NFLNK=5, NFSOCK=6, NFFIFO=7 */
int nfs_stat_types[9] = {
	0, S_IFREG, S_IFDIR, S_IFBLK, S_IFCHR, S_IFLNK, S_IFSOCK, S_IFIFO, 0 };

int
nfs_stat(struct open_file *f, struct stat *sb)
{
	struct nfs_iodesc *fp = (struct nfs_iodesc *)f->f_fsdata;
	uint32_t ftype, mode;

	ftype = ntohl(fp->fa.fa_type);
	mode  = ntohl(fp->fa.fa_mode);
	mode |= nfs_stat_types[ftype & 7];

	sb->st_mode  = mode;
	sb->st_nlink = ntohl(fp->fa.fa_nlink);
	sb->st_uid   = ntohl(fp->fa.fa_uid);
	sb->st_gid   = ntohl(fp->fa.fa_gid);
	sb->st_size  = ntohl(fp->fa.fa_size.val[1]);

	return (0);
}

static int
nfs_readdir(struct open_file *f, struct dirent *d)
{
	struct nfs_iodesc *fp = (struct nfs_iodesc *)f->f_fsdata;
	struct nfsv3_readdir_repl *repl;
	struct nfsv3_readdir_entry *rent;
	static void *pkt = NULL;
	static char *buf;
	static struct nfs_iodesc *pfp = NULL;
	static uint64_t cookie = 0;
	size_t cc;
	int pos, rc;

	struct args {
		uint32_t fhsize;
		uint32_t fhpluscookie[5 + NFS_V3MAXFHSIZE];
	} *args;
	struct {
		uint32_t h[RPC_HEADER_WORDS];
		struct args d;
	} sdata;

	if (fp != pfp || fp->off != cookie) {
		pfp = NULL;
	refill:
		free(pkt);
		pkt = NULL;
		args = &sdata.d;
		bzero(args, sizeof(*args));

		args->fhsize = htonl(fp->fhsize);
		bcopy(fp->fh, args->fhpluscookie, fp->fhsize);
		pos = roundup(fp->fhsize, sizeof(uint32_t)) / sizeof(uint32_t);
		args->fhpluscookie[pos++] = htonl(fp->off >> 32);
		args->fhpluscookie[pos++] = htonl(fp->off);
		args->fhpluscookie[pos++] = htonl(fp->cookie >> 32);
		args->fhpluscookie[pos++] = htonl(fp->cookie);
		args->fhpluscookie[pos] = htonl(NFS_READDIRSIZE);

		cc = rpc_call(fp->iodesc, NFS_PROG, NFS_VER3, NFSPROCV3_READDIR,
		    args, 6 * sizeof(uint32_t) +
		    roundup(fp->fhsize, sizeof(uint32_t)),
		    (void **)&buf, &pkt);
		if (cc == -1) {
			rc = errno;
			goto err;
		}
		repl = (struct nfsv3_readdir_repl *)buf;
		if (repl->errno != 0) {
			rc = ntohl(repl->errno);
			goto err;
		}
		pfp = fp;
		cookie = fp->off;
		fp->cookie = ((uint64_t)ntohl(repl->cookiev0) << 32) |
		    ntohl(repl->cookiev1);
		buf += sizeof (struct nfsv3_readdir_repl);
	}
	rent = (struct nfsv3_readdir_entry *)buf;

	if (rent->follows == 0) {
		/* fid0 is actually eof */
		if (rent->fid0 != 0) {
			rc = ENOENT;
			goto err;
		}
		goto refill;
	}

	d->d_namlen = ntohl(rent->len);
	bcopy(rent->nameplus, d->d_name, d->d_namlen);
	d->d_name[d->d_namlen] = '\0';

	pos = roundup(d->d_namlen, sizeof(uint32_t)) / sizeof(uint32_t);
	fp->off = cookie = ((uint64_t)ntohl(rent->nameplus[pos]) << 32) |
	    ntohl(rent->nameplus[pos + 1]);
	pos += 2;
	buf = (char *)&rent->nameplus[pos];
	return (0);

err:
	free(pkt);
	pkt = NULL;
	pfp = NULL;
	cookie = 0;
	return (rc);
}
