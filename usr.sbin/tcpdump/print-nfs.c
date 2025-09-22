/*	$OpenBSD: print-nfs.c,v 1.25 2021/12/01 18:28:46 deraadt Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <sys/time.h>
#include <sys/socket.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ip_var.h>

#include <rpc/rpc.h>

#include <ctype.h>
#include <pcap.h>
#include <stdio.h>
#include <string.h>

#include "interface.h"
#include "addrtoname.h"

#include "nfs.h"
#include "nfsfh.h"

static void nfs_printfh(const u_int32_t *, const u_int);
static void xid_map_enter(const struct rpc_msg *, const u_char *);
static int32_t xid_map_find(const struct rpc_msg *, const u_char *,
			    u_int32_t *, u_int32_t *);
static void interp_reply(const struct rpc_msg *, u_int32_t, u_int32_t, int);
static const u_int32_t *parse_post_op_attr(const u_int32_t *, int);
static void print_sattr3(const struct nfsv3_sattr *sa3, int verbose);
static int print_int64(const u_int32_t *dp, int how);

/*
 * Mapping of old NFS Version 2 RPC numbers to generic numbers.
 */
u_int32_t nfsv3_procid[NFS_NPROCS] = {
	NFSPROC_NULL,
	NFSPROC_GETATTR,
	NFSPROC_SETATTR,
	NFSPROC_NOOP,
	NFSPROC_LOOKUP,
	NFSPROC_READLINK,
	NFSPROC_READ,
	NFSPROC_NOOP,
	NFSPROC_WRITE,
	NFSPROC_CREATE,
	NFSPROC_REMOVE,
	NFSPROC_RENAME,
	NFSPROC_LINK,
	NFSPROC_SYMLINK,
	NFSPROC_MKDIR,
	NFSPROC_RMDIR,
	NFSPROC_READDIR,
	NFSPROC_FSSTAT,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP
};


static struct tok nfsvers2str[] = {
	{ NFS_VER2,	"NFSv2" },
	{ NFS_VER3,	"NFSv3" },
	{ NFS_VER4,	"NFSv4" },
	{ 0, NULL }
};

/*
 * NFS V2 and V3 status values.
 *
 * Some of these come from the RFCs for NFS V2 and V3, with the message
 * strings taken from the FreeBSD C library "errlst.c".
 *
 * Others are errors that are not in the RFC but that I suspect some
 * NFS servers could return; the values are FreeBSD errno values, as
 * the first NFS server was the SunOS 2.0 one, and until 5.0 SunOS
 * was primarily BSD-derived.
 */
static struct tok status2str[] = {
	{ 1,     "Operation not permitted" },	/* EPERM */
	{ 2,     "No such file or directory" },	/* ENOENT */
	{ 5,     "Input/output error" },	/* EIO */
	{ 6,     "Device not configured" },	/* ENXIO */
	{ 11,    "Resource deadlock avoided" },	/* EDEADLK */
	{ 12,    "Cannot allocate memory" },	/* ENOMEM */
	{ 13,    "Permission denied" },		/* EACCES */
	{ 17,    "File exists" },		/* EEXIST */
	{ 18,    "Cross-device link" },		/* EXDEV */
	{ 19,    "Operation not supported by device" }, /* ENODEV */
	{ 20,    "Not a directory" },		/* ENOTDIR */
	{ 21,    "Is a directory" },		/* EISDIR */
	{ 22,    "Invalid argument" },		/* EINVAL */
	{ 26,    "Text file busy" },		/* ETXTBSY */
	{ 27,    "File too large" },		/* EFBIG */
	{ 28,    "No space left on device" },	/* ENOSPC */
	{ 30,    "Read-only file system" },	/* EROFS */
	{ 31,    "Too many links" },		/* EMLINK */
	{ 45,    "Operation not supported" },	/* EOPNOTSUPP */
	{ 62,    "Too many levels of symbolic links" }, /* ELOOP */
	{ 63,    "File name too long" },	/* ENAMETOOLONG */
	{ 66,    "Directory not empty" },	/* ENOTEMPTY */
	{ 69,    "Disc quota exceeded" },	/* EDQUOT */
	{ 70,    "Stale NFS file handle" },	/* ESTALE */
	{ 71,    "Too many levels of remote in path" }, /* EREMOTE */
	{ 99,    "Write cache flushed to disk" }, /* NFSERR_WFLUSH (not used) */
	{ 10001, "Illegal NFS file handle" },	/* NFS3ERR_BADHANDLE */
	{ 10002, "Update synchronization mismatch" }, /* NFS3ERR_NOT_SYNC */
	{ 10003, "READDIR/READDIRPLUS cookie is stale" }, /* NFS3ERR_BAD_COOKIE */
	{ 10004, "Operation not supported" },	/* NFS3ERR_NOTSUPP */
	{ 10005, "Buffer or request is too small" }, /* NFS3ERR_TOOSMALL */
	{ 10006, "Unspecified error on server" }, /* NFS3ERR_SERVERFAULT */
	{ 10007, "Object of that type not supported" }, /* NFS3ERR_BADTYPE */
	{ 10008, "Request couldn't be completed in time" }, /* NFS3ERR_JUKEBOX */
	{ 0,     NULL }
};

static struct tok nfsv3_writemodes[] = {
	{ 0,		"unstable" },
	{ 1,		"datasync" },
	{ 2,		"filesync" },
	{ 0,		NULL }
};

static struct tok type2str[] = {
	{ NFNON,	"NON" },
	{ NFREG,	"REG" },
	{ NFDIR,	"DIR" },
	{ NFBLK,	"BLK" },
	{ NFCHR,	"CHR" },
	{ NFLNK,	"LNK" },
	{ NFFIFO,	"FIFO" },
	{ 0,		NULL }
};

/*
 * Print out a 64-bit integer. This appears to be different on each system,
 * try to make the best of it. The integer stored as 2 consecutive XDR
 * encoded 32-bit integers, to which a pointer is passed.
 *
 * Assume that a system that has INT64_FORMAT defined, has a 64-bit
 * integer datatype and can print it.
 */ 

#define UNSIGNED 0
#define SIGNED   1
#define HEX      2

static int print_int64(const u_int32_t *dp, int how)
{
#ifdef INT64_FORMAT
	u_int64_t res;

	TCHECK(dp[1]);
	res = ((u_int64_t)ntohl(dp[0]) << 32) | (u_int64_t)ntohl(dp[1]);
	switch (how) {
	case SIGNED:
		printf(INT64_FORMAT, res);
		break;
	case UNSIGNED:
		printf(U_INT64_FORMAT, res);
		break;
	case HEX:
		printf(HEX_INT64_FORMAT, res);
		break;
	default:
		return (0);
	}
#else
	switch (how) {
	case SIGNED:
	case UNSIGNED:
	case HEX:
		TCHECK(dp[1]);
		if (dp[0])
			printf("0x%x%08x", (u_int32_t)ntohl(dp[0]),
			    (u_int32_t)ntohl(dp[1]));
		else
			printf("0x%x", (u_int32_t)ntohl(dp[1]));
		break;
	default:
		return (0);
	}
#endif
	return 1;

trunc:
	return (0);
}

static const u_int32_t *
parse_sattr3(const u_int32_t *dp, struct nfsv3_sattr *sa3)
{
	TCHECK(dp[0]);
	if ((sa3->sa_modeset = ntohl(*dp++))) {
		TCHECK(dp[0]);
		sa3->sa_mode = ntohl(*dp++);
	}

	TCHECK(dp[0]);
	if ((sa3->sa_uidset = ntohl(*dp++))) {
		TCHECK(dp[0]);
		sa3->sa_uid = ntohl(*dp++);
	}

	TCHECK(dp[0]);
	if ((sa3->sa_gidset = ntohl(*dp++))) {
		TCHECK(dp[0]);
		sa3->sa_gid = ntohl(*dp++);
	}

	TCHECK(dp[0]);
	if ((sa3->sa_sizeset = ntohl(*dp++))) {
		TCHECK(dp[0]);
		sa3->sa_size = ntohl(*dp++);
	}

	TCHECK(dp[0]);
	if ((sa3->sa_atimetype = ntohl(*dp++)) == NFSV3SATTRTIME_TOCLIENT) {
		TCHECK(dp[1]);
		sa3->sa_atime.nfsv3_sec = ntohl(*dp++);
		sa3->sa_atime.nfsv3_nsec = ntohl(*dp++);
	}

	TCHECK(dp[0]);
	if ((sa3->sa_mtimetype = ntohl(*dp++)) == NFSV3SATTRTIME_TOCLIENT) {
		TCHECK(dp[1]);
		sa3->sa_mtime.nfsv3_sec = ntohl(*dp++);
		sa3->sa_mtime.nfsv3_nsec = ntohl(*dp++);
	}

	return dp;
trunc:
	return NULL;
}

static int nfserr;		/* true if we error rather than trunc */

static void
print_sattr3(const struct nfsv3_sattr *sa3, int verbose)
{
	if (sa3->sa_modeset)
		printf(" mode %o", sa3->sa_mode);
	if (sa3->sa_uidset)
		printf(" uid %u", sa3->sa_uid);
	if (sa3->sa_gidset)
		printf(" gid %u", sa3->sa_gid);
	if (verbose > 1) {
		if (sa3->sa_atimetype == NFSV3SATTRTIME_TOCLIENT)
			printf(" atime %u.%06u", sa3->sa_atime.nfsv3_sec,
			       sa3->sa_atime.nfsv3_nsec);
		if (sa3->sa_mtimetype == NFSV3SATTRTIME_TOCLIENT)
			printf(" mtime %u.%06u", sa3->sa_mtime.nfsv3_sec,
			       sa3->sa_mtime.nfsv3_nsec);
	}
}

void
nfsreply_print(const u_char *bp, u_int length, const u_char *bp2)
{
	const struct rpc_msg *rp;
	u_int32_t proc, vers;

	nfserr = 0;		/* assume no error */
	rp = (const struct rpc_msg *)bp;

	printf("xid 0x%x reply %s %d", (u_int32_t)ntohl(rp->rm_xid),
		ntohl(rp->rm_reply.rp_stat) == MSG_ACCEPTED ? "ok":"ERR",
		length);
	if (xid_map_find(rp, bp2, &proc, &vers) >= 0)
		interp_reply(rp, proc, vers, length);
}

/*
 * Return a pointer to the first file handle in the packet.
 * If the packet was truncated, return 0.
 */
static const u_int32_t *
parsereq(const struct rpc_msg *rp, u_int length)
{
	const u_int32_t *dp;
	u_int len;

	/*
	 * find the start of the req data (if we captured it)
	 */
	dp = (u_int32_t *)&rp->rm_call.cb_cred;
	TCHECK(dp[1]);
	len = ntohl(dp[1]);
	if (len < length) {
		dp += (len + (2 * sizeof(*dp) + 3)) / sizeof(*dp);
		TCHECK(dp[1]);
		len = ntohl(dp[1]);
		if (len < length) {
			dp += (len + (2 * sizeof(*dp) + 3)) / sizeof(*dp);
			TCHECK2(dp[0], 0);
			return (dp);
		}
	}
trunc:
	return (NULL);
}

/*
 * Print out an NFS file handle and return a pointer to following word.
 * If packet was truncated, return 0.
 */
static const u_int32_t *
parsefh(const u_int32_t *dp, int v3)
{
	int len;

	if (v3) {
		TCHECK(dp[0]);
		len = (int)ntohl(*dp) / 4;
		dp++;
	} else
		len = NFSX_V2FH / 4;

	if (TTEST2(*dp, len * sizeof(*dp))) {
		nfs_printfh(dp, len);
		return (dp + len);
	}
trunc:
	return (NULL);
}

/*
 * Print out a file name and return pointer to 32-bit word past it.
 * If packet was truncated, return 0.
 */
static const u_int32_t *
parsefn(const u_int32_t *dp)
{
	u_int32_t len;
	const u_char *cp;

	/* Bail if we don't have the string length */
	TCHECK(*dp);

	/* Fetch string length; convert to host order */
	len = *dp++;
	NTOHL(len);

	TCHECK2(*dp, ((len + 3) & ~3));

	cp = (u_char *)dp;
	/* Update 32-bit pointer (NFS filenames padded to 32-bit boundaries) */
	dp += ((len + 3) & ~3) / sizeof(*dp);
	putchar('"');
	if (fn_printn(cp, len, snapend)) {
		putchar('"');
		goto trunc;
	}
	putchar('"');

	return (dp);
trunc:
	return NULL;
}

/*
 * Print out file handle and file name.
 * Return pointer to 32-bit word past file name.
 * If packet was truncated (or there was some other error), return 0.
 */
static const u_int32_t *
parsefhn(const u_int32_t *dp, int v3)
{
	dp = parsefh(dp, v3);
	if (dp == NULL)
		return (NULL);
	putchar(' ');
	return (parsefn(dp));
}

void
nfsreq_print(const u_char *bp, u_int length, const u_char *bp2)
{
	const struct rpc_msg *rp;
	const u_int32_t *dp;
	nfstype type;
	int vers;
	int v3 = 0;
	u_int32_t proc;
	struct nfsv3_sattr sa3;

	nfserr = 0;		/* assume no error */
	rp = (const struct rpc_msg *)bp;

	vers = ntohl(rp->rm_call.cb_vers);
	if (vers == NFS_VER3)
		v3 = 1;

	printf("xid 0x%x (%s) %d", (u_int32_t)ntohl(rp->rm_xid),
	    tok2str(nfsvers2str, "Unk %i", vers), length);

	xid_map_enter(rp, bp2);	/* record proc number for later on */
	proc = ntohl(rp->rm_call.cb_proc);

	if (!v3 && proc < NFS_NPROCS)
		proc =  nfsv3_procid[proc];

	switch (proc) {
	case NFSPROC_NOOP:
		printf(" nop");
		return;
	case NFSPROC_NULL:
		printf(" null");
		return;

	case NFSPROC_GETATTR:
		printf(" getattr");
		if ((dp = parsereq(rp, length)) != NULL &&
		    parsefh(dp, v3) != NULL)
			return;
		break;

	case NFSPROC_SETATTR:
		printf(" setattr");
		if ((dp = parsereq(rp, length)) != NULL &&
		    parsefh(dp, v3) != NULL)
			return;
		break;

	case NFSPROC_LOOKUP:
		printf(" lookup");
		if ((dp = parsereq(rp, length)) != NULL &&
		    parsefhn(dp, v3) != NULL)
			return;
		break;

	case NFSPROC_ACCESS:
		printf(" access");
		if ((dp = parsereq(rp, length)) != NULL &&
		    (dp = parsefh(dp, v3)) != NULL) {
			TCHECK(dp[0]);
			printf(" %04x", (u_int32_t)ntohl(dp[0]));
			return;
		}
		break;

	case NFSPROC_READLINK:
		printf(" readlink");
		if ((dp = parsereq(rp, length)) != NULL &&
		    parsefh(dp, v3) != NULL)
			return;
		break;

	case NFSPROC_READ:
		printf(" read");
		if ((dp = parsereq(rp, length)) != NULL &&
		    (dp = parsefh(dp, v3)) != NULL) {
			if (v3) {
				TCHECK(dp[2]);
				printf(" %u bytes @ ",
				       (u_int32_t) ntohl(dp[2]));
				print_int64(dp, UNSIGNED);
			} else {
				TCHECK(dp[1]);
				printf(" %u bytes @ %u",
				    (u_int32_t)ntohl(dp[1]),
				    (u_int32_t)ntohl(dp[0]));
			}
			return;
		}
		break;

	case NFSPROC_WRITE:
		printf(" write");
		if ((dp = parsereq(rp, length)) != NULL &&
		    (dp = parsefh(dp, v3)) != NULL) {
			if (v3) {
				TCHECK(dp[4]);
				printf(" %u bytes @ ",
						(u_int32_t) ntohl(dp[4]));
				print_int64(dp, UNSIGNED);
				if (vflag) {
					dp += 3;
					TCHECK(dp[0]);
					printf(" <%s>",
						tok2str(nfsv3_writemodes,
							NULL, ntohl(*dp)));
				}
			} else {
				TCHECK(dp[3]);
				printf(" %u (%u) bytes @ %u (%u)",
						(u_int32_t)ntohl(dp[3]),
						(u_int32_t)ntohl(dp[2]),
						(u_int32_t)ntohl(dp[1]),
						(u_int32_t)ntohl(dp[0]));
			}
			return;
		}
		break;

	case NFSPROC_CREATE:
		printf(" create");
		if ((dp = parsereq(rp, length)) != NULL &&
		    parsefhn(dp, v3) != NULL)
			return;
		break;

	case NFSPROC_MKDIR:
		printf(" mkdir");
		if ((dp = parsereq(rp, length)) != 0 && parsefhn(dp, v3) != 0)
			return;
		break;

	case NFSPROC_SYMLINK:
		printf(" symlink");
		if ((dp = parsereq(rp, length)) != 0 &&
		    (dp = parsefhn(dp, v3)) != 0) {
			printf(" ->");
			if (v3 && (dp = parse_sattr3(dp, &sa3)) == 0)
				break;
			if (parsefn(dp) == 0)
				break;
			if (v3 && vflag)
				print_sattr3(&sa3, vflag);
			return;
		}
		break;

	case NFSPROC_MKNOD:
		printf(" mknod");
		if ((dp = parsereq(rp, length)) != 0 &&
		    (dp = parsefhn(dp, v3)) != 0) {
			TCHECK(*dp);
			type = (nfstype)ntohl(*dp++);
			if ((dp = parse_sattr3(dp, &sa3)) == 0)
				break;
			printf(" %s", tok2str(type2str, "unk-ft %d", type));
			if (vflag && (type == NFCHR || type == NFBLK)) {
				TCHECK(dp[1]);
				printf(" %u/%u",
				       (u_int32_t)ntohl(dp[0]),
				       (u_int32_t)ntohl(dp[1]));
				dp += 2;
			}
			if (vflag)
				print_sattr3(&sa3, vflag);
			return;
		}
		break;

	case NFSPROC_REMOVE:
		printf(" remove");
		if ((dp = parsereq(rp, length)) != NULL &&
		    parsefhn(dp, v3) != NULL)
			return;
		break;

	case NFSPROC_RMDIR:
		printf(" rmdir");
		if ((dp = parsereq(rp, length)) != NULL &&
		    parsefhn(dp, v3) != NULL)
			return;
		break;

	case NFSPROC_RENAME:
		printf(" rename");
		if ((dp = parsereq(rp, length)) != NULL &&
		    (dp = parsefhn(dp, v3)) != NULL) {
			printf(" ->");
			if (parsefhn(dp, v3) != NULL)
				return;
		}
		break;

	case NFSPROC_LINK:
		printf(" link");
		if ((dp = parsereq(rp, length)) != NULL &&
		    (dp = parsefh(dp, v3)) != NULL) {
			printf(" ->");
			if (parsefhn(dp, v3) != NULL)
				return;
		}
		break;

	case NFSPROC_READDIR:
		printf(" readdir");
		if ((dp = parsereq(rp, length)) != NULL &&
		    (dp = parsefh(dp, v3)) != NULL) {
			if (v3) {
				TCHECK(dp[4]);
				/*
				 * We shouldn't really try to interpret the
				 * offset cookie here.
				 */
				printf(" %u bytes @ ",
				    (u_int32_t) ntohl(dp[4]));
				print_int64(dp, SIGNED);
				if (vflag)
					printf(" verf %08x%08x", dp[2],
					       dp[3]);
			} else {
				TCHECK(dp[1]);
				/*
				 * Print the offset as signed, since -1 is
				 * common, but offsets > 2^31 aren't.
				 */
				printf(" %u bytes @ %d",
				    (u_int32_t)ntohl(dp[1]),
				    (u_int32_t)ntohl(dp[0]));
			}
			return;
		}
		break;

	case NFSPROC_READDIRPLUS:
		printf(" readdirplus");
		if ((dp = parsereq(rp, length)) != NULL &&
		    (dp = parsefh(dp, v3)) != NULL) {
			TCHECK(dp[4]);
			/*
			 * We don't try to interpret the offset
			 * cookie here.
			 */
			printf(" %u bytes @ ", (u_int32_t) ntohl(dp[4]));
			print_int64(dp, SIGNED);
			if (vflag) {
				TCHECK(dp[5]);
				printf(" max %u verf %08x%08x",
				       (u_int32_t) ntohl(dp[5]), dp[2], dp[3]);
			}
			return;
		}
		break;

	case NFSPROC_FSSTAT:
		printf(" fsstat");
		if ((dp = parsereq(rp, length)) != NULL &&
		    parsefh(dp, v3) != NULL)
			return;
		break;

	case NFSPROC_FSINFO:
		printf(" fsinfo");
		if ((dp = parsereq(rp, length)) != NULL &&
		    parsefh(dp, v3) != NULL)
			return;
		break;

	case NFSPROC_PATHCONF:
		printf(" pathconf");
		if ((dp = parsereq(rp, length)) != NULL &&
		    parsefh(dp, v3) != NULL)
			return;
		break;

	case NFSPROC_COMMIT:
		printf(" commit");
		if ((dp = parsereq(rp, length)) != NULL &&
		    (dp = parsefh(dp, v3)) != NULL) {
			TCHECK(dp[2]);
			printf(" %u bytes @ ", (u_int32_t) ntohl(dp[2]));
			print_int64(dp, UNSIGNED);
			return;
		}
		break;

	default:
		printf(" proc-%u", (u_int32_t)ntohl(rp->rm_call.cb_proc));
		return;
	}

trunc:
	if (!nfserr)
		printf(" [|nfs]");
}

/*
 * Print out an NFS file handle.
 * We assume packet was not truncated before the end of the
 * file handle pointed to by dp.
 *
 * Note: new version (using portable file-handle parser) doesn't produce
 * generation number.  It probably could be made to do that, with some
 * additional hacking on the parser code.
 */
static void
nfs_printfh(const u_int32_t *dp, const u_int len)
{
	my_fsid fsid;
	ino_t ino;
	char *sfsname = NULL;

	Parse_fh((caddr_t *)dp, &fsid, &ino, NULL, &sfsname);

	if (sfsname) {
		/* file system ID is ASCII, not numeric, for this server OS */
		static char temp[NFSX_V3FHMAX+1];

		/* Make sure string is null-terminated */
		strlcpy(temp, sfsname, sizeof(temp));
		/* Remove trailing spaces */
		sfsname = strchr(temp, ' ');
		if (sfsname)
			*sfsname = 0;

		printf(" fh %s/%u", temp, (u_int32_t)ino);
	} else {
		printf(" fh %u,%u/%u",
		    fsid.Fsid_dev.Major, fsid.Fsid_dev.Minor, (u_int32_t)ino);
	}
}

/*
 * Maintain a small cache of recent client.XID.server/proc pairs, to allow
 * us to match up replies with requests and thus to know how to parse
 * the reply.
 */

struct xid_map_entry {
	u_int32_t	xid;		/* transaction ID (net order) */
	int ipver;			/* IP version (4 or 6) */
	struct in6_addr	client;		/* client IP address (net order) */
	struct in6_addr	server;		/* server IP address (net order) */
	u_int32_t	proc;		/* call proc number (host order) */
	u_int32_t	vers;		/* program version (host order) */
};

/*
 * Map entries are kept in an array that we manage as a ring;
 * new entries are always added at the tail of the ring.  Initially,
 * all the entries are zero and hence don't match anything.
 */

#define	XIDMAPSIZE	64

struct xid_map_entry xid_map[XIDMAPSIZE];

int	xid_map_next = 0;
int	xid_map_hint = 0;

static void
xid_map_enter(const struct rpc_msg *rp, const u_char *bp)
{
	struct ip *ip = NULL;
	struct ip6_hdr *ip6 = NULL;
	struct xid_map_entry *xmep;

	ip = (struct ip *)bp;

	xmep = &xid_map[xid_map_next];

	if (++xid_map_next >= XIDMAPSIZE)
		xid_map_next = 0;

	xmep->xid = rp->rm_xid;
	xmep->ipver = ip->ip_v;

	switch (xmep->ipver) {
	case 4:
		memcpy(&xmep->client, &ip->ip_src, sizeof(ip->ip_src));
		memcpy(&xmep->server, &ip->ip_dst, sizeof(ip->ip_dst));
		break;
	case 6:
		ip6 = (struct ip6_hdr *)bp;
		memcpy(&xmep->client, &ip6->ip6_src, sizeof(ip6->ip6_src));
		memcpy(&xmep->server, &ip6->ip6_dst, sizeof(ip6->ip6_dst));
		break;
	default:
		return;
	}
	xmep->proc = ntohl(rp->rm_call.cb_proc);
	xmep->vers = ntohl(rp->rm_call.cb_vers);
}

/*
 * Returns 0 and puts NFSPROC_xxx in proc return and
 * version in vers return, or returns -1 on failure
 */
static int
xid_map_find(const struct rpc_msg *rp, const u_char *bp, u_int32_t *proc,
	     u_int32_t *vers)
{
	int i;
	struct xid_map_entry *xmep;
	u_int32_t xid = rp->rm_xid;
	struct ip *ip = (struct ip *)bp;
	struct ip6_hdr *ip6 = (struct ip6_hdr *)bp;
	int cmp;

	/* Start searching from where we last left off */
	i = xid_map_hint; 
	do {
		xmep = &xid_map[i];
		cmp = 1;
		if (xmep->ipver != ip->ip_v || xmep->xid != xid)
			goto nextitem;
		switch (xmep->ipver) {
		case 4:
			if (memcmp(&ip->ip_src, &xmep->server,
				   sizeof(ip->ip_src)) != 0 ||
			    memcmp(&ip->ip_dst, &xmep->client,
				   sizeof(ip->ip_dst)) != 0) {
				cmp = 0;
			}
			break;
		case 6:
			if (memcmp(&ip6->ip6_src, &xmep->server,
				   sizeof(ip6->ip6_src)) != 0 ||
			    memcmp(&ip6->ip6_dst, &xmep->client,
				   sizeof(ip6->ip6_dst)) != 0) {
				cmp = 0;
			}
			break;
		default:
			cmp = 0;
			break;
		}
		if (cmp) {
			/* match */
			xid_map_hint = i;
			*proc = xmep->proc;
			*vers = xmep->vers;
			return 0;
		}
	nextitem:
		if (++i >= XIDMAPSIZE)
			i = 0;
	} while (i != xid_map_hint);

	/* search failed */
	return (-1);
}

/*
 * Routines for parsing reply packets
 */

/*
 * Return a pointer to the beginning of the actual results.
 * If the packet was truncated, return 0.
 */
static const u_int32_t *
parserep(const struct rpc_msg *rp, u_int length)
{
	const u_int32_t *dp;
	u_int len;
	enum accept_stat astat;

	/*
	 * Portability note:
	 * Here we find the address of the ar_verf credentials.
	 * Originally, this calculation was
	 *	dp = (u_int32_t *)&rp->rm_reply.rp_acpt.ar_verf
	 * On the wire, the rp_acpt field starts immediately after
	 * the (32 bit) rp_stat field.  However, rp_acpt (which is a
	 * "struct accepted_reply") contains a "struct opaque_auth",
	 * whose internal representation contains a pointer, so on a
	 * 64-bit machine the compiler inserts 32 bits of padding
	 * before rp->rm_reply.rp_acpt.ar_verf.  So, we cannot use
	 * the internal representation to parse the on-the-wire
	 * representation.  Instead, we skip past the rp_stat field,
	 * which is an "enum" and so occupies one 32-bit word.
	 */
	dp = ((const u_int32_t *)&rp->rm_reply) + 1;
	TCHECK(dp[1]);
	len = ntohl(dp[1]);
	if (len >= length)
		return (NULL);
	/*
	 * skip past the ar_verf credentials.
	 */
	dp += (len + (2*sizeof(u_int32_t) + 3)) / sizeof(u_int32_t);
	TCHECK2(dp[0], 0);

	/*
	 * now we can check the ar_stat field
	 */
	astat = ntohl(*(enum accept_stat *)dp);
	switch (astat) {

	case SUCCESS:
		break;

	case PROG_UNAVAIL:
		printf(" PROG_UNAVAIL");
		nfserr = 1;		/* suppress trunc string */
		return (NULL);

	case PROG_MISMATCH:
		printf(" PROG_MISMATCH");
		nfserr = 1;		/* suppress trunc string */
		return (NULL);

	case PROC_UNAVAIL:
		printf(" PROC_UNAVAIL");
		nfserr = 1;		/* suppress trunc string */
		return (NULL);

	case GARBAGE_ARGS:
		printf(" GARBAGE_ARGS");
		nfserr = 1;		/* suppress trunc string */
		return (NULL);

	case SYSTEM_ERR:
		printf(" SYSTEM_ERR");
		nfserr = 1;		/* suppress trunc string */
		return (NULL);

	default:
		printf(" ar_stat %d", astat);
		nfserr = 1;		/* suppress trunc string */
		return (NULL);
	}
	/* successful return */
	TCHECK2(*dp, sizeof(astat));
	return ((u_int32_t *) (sizeof(astat) + ((char *)dp)));
trunc:
	return (0);
}

static const u_int32_t *
parsestatus(const u_int32_t *dp, int *er)
{
	int errnum;

	TCHECK(dp[0]);

	errnum = ntohl(dp[0]);
	if (er)
		*er = errnum;
	if (errnum != 0) {
		if (!qflag)
			printf(" ERROR: %s",
			    tok2str(status2str, "unk %d", errnum));
		nfserr = 1;
	}
	return (dp + 1);
trunc:
	return NULL;
}

static const u_int32_t *
parsefattr(const u_int32_t *dp, int verbose, int v3)
{
	const struct nfs_fattr *fap;

	fap = (const struct nfs_fattr *)dp;
	TCHECK(fap->fa_gid);
	if (verbose) {
		printf(" %s %o ids %d/%d",
		    tok2str(type2str, "unk-ft %d ",
		    (u_int32_t)ntohl(fap->fa_type)),
		    (u_int32_t)ntohl(fap->fa_mode),
		    (u_int32_t)ntohl(fap->fa_uid),
		    (u_int32_t) ntohl(fap->fa_gid));
		if (v3) {
			TCHECK(fap->fa3_size);
			printf(" sz ");
			print_int64((u_int32_t *)&fap->fa3_size, UNSIGNED);
		} else {
			TCHECK(fap->fa2_size);
			printf(" sz %d", (u_int32_t) ntohl(fap->fa2_size));
		}
	}
	/* print lots more stuff */
	if (verbose > 1) {
		if (v3) {
			TCHECK(fap->fa3_ctime);
			printf(" nlink %d rdev %d/%d",
			       (u_int32_t)ntohl(fap->fa_nlink),
			       (u_int32_t) ntohl(fap->fa3_rdev.specdata1),
			       (u_int32_t) ntohl(fap->fa3_rdev.specdata2));
			printf(" fsid ");
			print_int64((u_int32_t *)&fap->fa3_fsid, HEX);
			printf(" fileid ");
			print_int64((u_int32_t *)&fap->fa3_fileid, HEX);
			printf(" a/m/ctime %u.%06u",
			       (u_int32_t) ntohl(fap->fa3_atime.nfsv3_sec),
			       (u_int32_t) ntohl(fap->fa3_atime.nfsv3_nsec));
			printf(" %u.%06u",
			       (u_int32_t) ntohl(fap->fa3_mtime.nfsv3_sec),
			       (u_int32_t) ntohl(fap->fa3_mtime.nfsv3_nsec));
			printf(" %u.%06u",
			       (u_int32_t) ntohl(fap->fa3_ctime.nfsv3_sec),
			       (u_int32_t) ntohl(fap->fa3_ctime.nfsv3_nsec));
		} else {
			TCHECK(fap->fa2_ctime);
			printf("nlink %d rdev %x fsid %x fileid %x a/m/ctime",
			       (u_int32_t) ntohl(fap->fa_nlink),
			       (u_int32_t) ntohl(fap->fa2_rdev),
			       (u_int32_t) ntohl(fap->fa2_fsid),
			       (u_int32_t) ntohl(fap->fa2_fileid));
			printf(" %u.%06u",
			       (u_int32_t) ntohl(fap->fa2_atime.nfsv2_sec),
			       (u_int32_t) ntohl(fap->fa2_atime.nfsv2_usec));
			printf(" %u.%06u",
			       (u_int32_t) ntohl(fap->fa2_mtime.nfsv2_sec),
			       (u_int32_t) ntohl(fap->fa2_mtime.nfsv2_usec));
			printf(" %u.%06u",
			       (u_int32_t) ntohl(fap->fa2_ctime.nfsv2_sec),
			       (u_int32_t) ntohl(fap->fa2_ctime.nfsv2_usec));
		}
	}
	return ((const u_int32_t *)((unsigned char *)dp +
		(v3 ? NFSX_V3FATTR : NFSX_V2FATTR)));
trunc:
	return (NULL);
}

static int
parseattrstat(const u_int32_t *dp, int verbose, int v3)
{
	int er;

	dp = parsestatus(dp, &er);
	if (dp == NULL)
		return (0);
	if (er)
		return (1);

	return (parsefattr(dp, verbose, v3) != NULL);
}

static int
parsediropres(const u_int32_t *dp)
{
	int er;

	if (!(dp = parsestatus(dp, &er)))
		return (0);
	if (er)
		return (1);

	dp = parsefh(dp, 0);
	if (dp == NULL)
		return (0);

	return (parsefattr(dp, vflag, 0) != NULL);
}

static int
parselinkres(const u_int32_t *dp, int v3)
{
	int er;

	dp = parsestatus(dp, &er);
	if (dp == NULL)
		return(0);
	if (er)
		return(1);
	if (v3 && !(dp = parse_post_op_attr(dp, vflag)))
		return (0);
	putchar(' ');
	return (parsefn(dp) != NULL);
}

static int
parsestatfs(const u_int32_t *dp, int v3)
{
	const struct nfs_statfs *sfsp;
	int er;

	dp = parsestatus(dp, &er);
	if (dp == NULL)
		return (0);
	if (!v3 && er)
		return (1);

	if (qflag)
		return(1);

	if (v3) {
		if (vflag)
			printf(" POST:");
		if (!(dp = parse_post_op_attr(dp, vflag)))
			return (0);
	}

	TCHECK2(*dp, (v3 ? NFSX_V3STATFS : NFSX_V2STATFS));

	sfsp = (const struct nfs_statfs *)dp;

	if (v3) {
		printf(" tbytes ");
		print_int64((u_int32_t *)&sfsp->sf_tbytes, UNSIGNED);
		printf(" fbytes ");
		print_int64((u_int32_t *)&sfsp->sf_fbytes, UNSIGNED);
		printf(" abytes ");
		print_int64((u_int32_t *)&sfsp->sf_abytes, UNSIGNED);
		if (vflag) {
			printf(" tfiles ");
			print_int64((u_int32_t *)&sfsp->sf_tfiles, UNSIGNED);
			printf(" ffiles ");
			print_int64((u_int32_t *)&sfsp->sf_ffiles, UNSIGNED);
			printf(" afiles ");
			print_int64((u_int32_t *)&sfsp->sf_afiles, UNSIGNED);
			printf(" invar %u",
			       (u_int32_t) ntohl(sfsp->sf_invarsec));
		}
	} else {
		printf(" tsize %d bsize %d blocks %d bfree %d bavail %d",
			(u_int32_t)ntohl(sfsp->sf_tsize),
			(u_int32_t)ntohl(sfsp->sf_bsize),
			(u_int32_t)ntohl(sfsp->sf_blocks),
			(u_int32_t)ntohl(sfsp->sf_bfree),
			(u_int32_t)ntohl(sfsp->sf_bavail));
	}

	return (1);
trunc:
	return (0);
}

static int
parserddires(const u_int32_t *dp)
{
	int er;

	dp = parsestatus(dp, &er);
	if (dp == NULL)
		return (0);
	if (er)
		return (1);
	if (qflag)
		return (1);

	TCHECK(dp[2]);
	printf(" offset %x size %d ",
	       (u_int32_t)ntohl(dp[0]), (u_int32_t)ntohl(dp[1]));
	if (dp[2] != 0)
		printf(" eof");

	return (1);
trunc:
	return (0);
}

static const u_int32_t *
parse_wcc_attr(const u_int32_t *dp)
{
	printf(" sz ");
	print_int64(dp, UNSIGNED);
	TCHECK(dp[5]);
	printf(" mtime %u.%06u ctime %u.%06u",
	       (u_int32_t)ntohl(dp[2]), (u_int32_t)ntohl(dp[3]),
	       (u_int32_t)ntohl(dp[4]), (u_int32_t)ntohl(dp[5]));
	return (dp + 6);

trunc:
	return (NULL);
}

/*
 * Pre operation attributes. Print only if vflag > 1.
 */
static const u_int32_t *
parse_pre_op_attr(const u_int32_t *dp, int verbose)
{
	TCHECK(dp[0]);
	if (!ntohl(dp[0]))
		return (dp + 1);
	dp++;
	TCHECK2(*dp, 24);
	if (verbose > 1) {
		return parse_wcc_attr(dp);
	} else {
		/* If not verbose enough, just skip over wcc_attr */
		return (dp + 6);
	}
trunc:
	return (NULL);
}

/*
 * Post operation attributes are printed if vflag >= 1
 */
static const u_int32_t *
parse_post_op_attr(const u_int32_t *dp, int verbose)
{
	TCHECK(dp[0]);
	if (!ntohl(dp[0]))
		return (dp + 1);
	dp++;
	if (verbose) {
		return parsefattr(dp, verbose, 1);
	} else
		return (dp + (NFSX_V3FATTR / sizeof (u_int32_t)));
trunc:
	return (NULL);
}

static const u_int32_t *
parse_wcc_data(const u_int32_t *dp, int verbose)
{
	if (verbose > 1)
		printf(" PRE:");
	if (!(dp = parse_pre_op_attr(dp, verbose)))
		return (0);

	if (verbose)
		printf(" POST:");
	return parse_post_op_attr(dp, verbose);
}

static const u_int32_t *
parsecreateopres(const u_int32_t *dp, int verbose)
{
	int er;

	if (!(dp = parsestatus(dp, &er)))
		return (0);
	if (er)
		dp = parse_wcc_data(dp, verbose);
	else {
		TCHECK(dp[0]);
		if (!ntohl(dp[0]))
			return (dp + 1);
		dp++;
		if (!(dp = parsefh(dp, 1)))
			return (0);
		if (verbose) {
			if (!(dp = parse_post_op_attr(dp, verbose)))
				return (0);
			if (vflag > 1) {
				printf(" dir attr:");
				dp = parse_wcc_data(dp, verbose);
			}
		}
	}
	return (dp);
trunc:
	return (NULL);
}

static int
parsewccres(const u_int32_t *dp, int verbose)
{
	int er;

	if (!(dp = parsestatus(dp, &er)))
		return (0);
	return parse_wcc_data(dp, verbose) != 0;
}

static const u_int32_t *
parsev3rddirres(const u_int32_t *dp, int verbose)
{
	int er;

	if (!(dp = parsestatus(dp, &er)))
		return (0);
	if (vflag)
		printf(" POST:");
	if (!(dp = parse_post_op_attr(dp, verbose)))
		return (0);
	if (er)
		return dp;
	if (vflag) {
		TCHECK(dp[1]);
		printf(" verf %08x%08x", dp[0], dp[1]);
		dp += 2;
	}
	return dp;
trunc:
	return (NULL);
}

static int
parsefsinfo(const u_int32_t *dp)
{
	struct nfsv3_fsinfo *sfp;
	int er;

	if (!(dp = parsestatus(dp, &er)))
		return (0);
	if (vflag)
		printf(" POST:");
	if (!(dp = parse_post_op_attr(dp, vflag)))
		return (0);
	if (er)
		return (1);

	sfp = (struct nfsv3_fsinfo *)dp;
	TCHECK(*sfp);
	printf(" rtmax %u rtpref %u wtmax %u wtpref %u dtpref %u",
	       (u_int32_t) ntohl(sfp->fs_rtmax),
	       (u_int32_t) ntohl(sfp->fs_rtpref),
	       (u_int32_t) ntohl(sfp->fs_wtmax),
	       (u_int32_t) ntohl(sfp->fs_wtpref),
	       (u_int32_t) ntohl(sfp->fs_dtpref));
	if (vflag) {
		printf(" rtmult %u wtmult %u maxfsz ",
		       (u_int32_t) ntohl(sfp->fs_rtmult),
		       (u_int32_t) ntohl(sfp->fs_wtmult));
		print_int64((u_int32_t *)&sfp->fs_maxfilesize, UNSIGNED);
		printf(" delta %u.%06u ",
		       (u_int32_t) ntohl(sfp->fs_timedelta.nfsv3_sec),
		       (u_int32_t) ntohl(sfp->fs_timedelta.nfsv3_nsec));
	}
	return (1);
trunc:
	return (0);
}

static int
parsepathconf(const u_int32_t *dp)
{
	int er;
	struct nfsv3_pathconf *spp;

	if (!(dp = parsestatus(dp, &er)))
		return (0);
	if (vflag)
		printf(" POST:");
	if (!(dp = parse_post_op_attr(dp, vflag)))
		return (0);
	if (er)
		return (1);

	spp = (struct nfsv3_pathconf *)dp;
	TCHECK(*spp);

	printf(" linkmax %u namemax %u %s %s %s %s",
	       (u_int32_t) ntohl(spp->pc_linkmax),
	       (u_int32_t) ntohl(spp->pc_namemax),
	       ntohl(spp->pc_notrunc) ? "notrunc" : "",
	       ntohl(spp->pc_chownrestricted) ? "chownres" : "",
	       ntohl(spp->pc_caseinsensitive) ? "igncase" : "",
	       ntohl(spp->pc_casepreserving) ? "keepcase" : "");
	return (1);
trunc:
	return (0);
}

static void
interp_reply(const struct rpc_msg *rp, u_int32_t proc, u_int32_t vers, int length)
{
	const u_int32_t *dp;
	int v3;
	int er;

	v3 = (vers == NFS_VER3);

	if (!v3 && proc < NFS_NPROCS)
		proc = nfsv3_procid[proc];

	switch (proc) {

	case NFSPROC_NOOP:
		printf(" nop");
		return;

	case NFSPROC_NULL:
		printf(" null");
		return;

	case NFSPROC_GETATTR:
		printf(" getattr");
		dp = parserep(rp, length);
		if (dp != NULL && parseattrstat(dp, !qflag, v3) != 0)
			return;
		break;

	case NFSPROC_SETATTR:
		printf(" setattr");
		if (!(dp = parserep(rp, length)))
			return;
		if (v3) {
			if (parsewccres(dp, vflag))
				return;
		} else {
			if (parseattrstat(dp, !qflag, 0) != 0)
				return;
		}
		break;

	case NFSPROC_LOOKUP:
		printf(" lookup");
		if (!(dp = parserep(rp, length)))
			break;
		if (v3) {
			if (!(dp = parsestatus(dp, &er)))
				break;
			if (er) {
				if (vflag > 1) {
					printf(" post dattr:");
					dp = parse_post_op_attr(dp, vflag);
				}
			} else {
				if (!(dp = parsefh(dp, v3)))
					break;
				if ((dp = parse_post_op_attr(dp, vflag)) &&
				    vflag > 1) {
					printf(" post dattr:");
					dp = parse_post_op_attr(dp, vflag);
				}
			}
			if (dp)
				return;
		} else {
			if (parsediropres(dp) != 0)
				return;
		}
		break;

	case NFSPROC_ACCESS:
		printf(" access");
		if (!(dp = parserep(rp, length)))
			break;
		if (!(dp = parsestatus(dp, &er)))
			break;
		if (vflag)
			printf(" attr:");
		if (!(dp = parse_post_op_attr(dp, vflag)))
			break;
		if (!er) {
			TCHECK(dp[0]);
			printf(" c %04x", (u_int32_t)ntohl(dp[0]));
		}
		return;

	case NFSPROC_READLINK:
		printf(" readlink");
		dp = parserep(rp, length);
		if (dp != NULL && parselinkres(dp, v3) != 0)
			return;
		break;

	case NFSPROC_READ:
		printf(" read");
		if (!(dp = parserep(rp, length)))
			break;
		if (v3) {
			if (!(dp = parsestatus(dp, &er)))
				break;
			if (!(dp = parse_post_op_attr(dp, vflag)))
				break;
			if (er)
				return;
			if (vflag) {
				TCHECK(dp[1]);
				printf(" %u bytes", (u_int32_t) ntohl(dp[0]));
				if (ntohl(dp[1]))
					printf(" EOF");
			}
			return;
		} else {
			if (parseattrstat(dp, vflag, 0) != 0)
				return;
		}
		break;

	case NFSPROC_WRITE:
		printf(" write");
		if (!(dp = parserep(rp, length)))
			break;
		if (v3) {
			if (!(dp = parsestatus(dp, &er)))
				break;
			if (!(dp = parse_wcc_data(dp, vflag)))
				break;
			if (er)
				return;
			if (vflag) {
				TCHECK(dp[0]);
				printf(" %u bytes", (u_int32_t) ntohl(dp[0]));
				if (vflag > 1) {
					TCHECK(dp[1]);
					printf(" <%s>",
						tok2str(nfsv3_writemodes,
							NULL, ntohl(dp[1])));
				}
				return;
			}
		} else {
			if (parseattrstat(dp, vflag, v3) != 0)
				return;
		}
		break;

	case NFSPROC_CREATE:
		printf(" create");
		if (!(dp = parserep(rp, length)))
			break;
		if (v3) {
			if (parsecreateopres(dp, vflag) != 0)
				return;
		} else {
			if (parsediropres(dp) != 0)
				return;
		}
		break;

	case NFSPROC_MKDIR:
		printf(" mkdir");
		if (!(dp = parserep(rp, length)))
			break;
		if (v3) {
			if (parsecreateopres(dp, vflag) != 0)
				return;
		} else {
			if (parsediropres(dp) != 0)
				return;
		}
		break;

	case NFSPROC_SYMLINK:
		printf(" symlink");
		if (!(dp = parserep(rp, length)))
			break;
		if (v3) {
			if (parsecreateopres(dp, vflag) != 0)
				return;
		} else {
			if (parsestatus(dp, &er) != 0)
				return;
		}
		break;

	case NFSPROC_MKNOD:
		printf(" mknod");
		if (!(dp = parserep(rp, length)))
			break;
		if (parsecreateopres(dp, vflag) != 0)
			return;
		break;

	case NFSPROC_REMOVE:
		printf(" remove");
		if (!(dp = parserep(rp, length)))
			break;
		if (v3) {
			if (parsewccres(dp, vflag))
				return;
		} else {
			if (parsestatus(dp, &er) != 0)
				return;
		}
		break;

	case NFSPROC_RMDIR:
		printf(" rmdir");
		if (!(dp = parserep(rp, length)))
			break;
		if (v3) {
			if (parsewccres(dp, vflag))
				return;
		} else {
			if (parsestatus(dp, &er) != 0)
				return;
		}
		break;

	case NFSPROC_RENAME:
		printf(" rename");
		if (!(dp = parserep(rp, length)))
			break;
		if (v3) {
			if (!(dp = parsestatus(dp, &er)))
				break;
			if (vflag) {
				printf(" from:");
				if (!(dp = parse_wcc_data(dp, vflag)))
					break;
				printf(" to:");
				if (!(dp = parse_wcc_data(dp, vflag)))
					break;
			}
			return;
		} else {
			if (parsestatus(dp, &er) != 0)
				return;
		}
		break;

	case NFSPROC_LINK:
		printf(" link");
		if (!(dp = parserep(rp, length)))
			break;
		if (v3) {
			if (!(dp = parsestatus(dp, &er)))
				break;
			if (vflag) {
				printf(" file POST:");
				if (!(dp = parse_post_op_attr(dp, vflag)))
					break;
				printf(" dir:");
				if (!(dp = parse_wcc_data(dp, vflag)))
					break;
				return;
			}
		} else {
			if (parsestatus(dp, &er) != 0)
				return;
		}
		break;

	case NFSPROC_READDIR:
		printf(" readdir");
		if (!(dp = parserep(rp, length)))
			break;
		if (v3) {
			if (parsev3rddirres(dp, vflag))
				return;
		} else {
			if (parserddires(dp) != 0)
				return;
		}
		break;

	case NFSPROC_READDIRPLUS:
		printf(" readdirplus");
		if (!(dp = parserep(rp, length)))
			break;
		if (parsev3rddirres(dp, vflag))
			return;
		break;

	case NFSPROC_FSSTAT:
		printf(" fsstat");
		dp = parserep(rp, length);
		if (dp != NULL && parsestatfs(dp, v3) != 0)
			return;
		break;

	case NFSPROC_FSINFO:
		printf(" fsinfo");
		dp = parserep(rp, length);
		if (dp != NULL && parsefsinfo(dp) != 0)
			return;
		break;

	case NFSPROC_PATHCONF:
		printf(" pathconf");
		dp = parserep(rp, length);
		if (dp != NULL && parsepathconf(dp) != 0)
			return;
		break;

	case NFSPROC_COMMIT:
		printf(" commit");
		dp = parserep(rp, length);
		if (dp != NULL && parsewccres(dp, vflag) != 0)
			return;
		break;

	default:
		printf(" proc-%u", proc);
		return;
	}
trunc:
	if (!nfserr)
		printf(" [|nfs]");
}
