/*
 * NFS protocol definitions
 *
 * This file contains constants mostly for Version 2 of the protocol,
 * but also has a couple of NFSv3 bits in (notably the error codes).
 */
#ifndef _LINUX_NFS_H
#define _LINUX_NFS_H

#define NFS_PROGRAM	100003
#define NFS_PORT	2049
#define NFS_MAXDATA	8192
#define NFS_MAXPATHLEN	1024
#define NFS_MAXNAMLEN	255
#define NFS_MAXGROUPS	16
#define NFS_FHSIZE	32
#define NFS_COOKIESIZE	4
#define NFS_FIFO_DEV	(-1)
#define NFSMODE_FMT	0170000
#define NFSMODE_DIR	0040000
#define NFSMODE_CHR	0020000
#define NFSMODE_BLK	0060000
#define NFSMODE_REG	0100000
#define NFSMODE_LNK	0120000
#define NFSMODE_SOCK	0140000
#define NFSMODE_FIFO	0010000

#define NFS_MNT_PROGRAM		100005
#define NFS_MNT_VERSION		1
#define NFS_MNT3_VERSION	3

/*
 * NFS stats. The good thing with these values is that NFSv3 errors are
 * a superset of NFSv2 errors (with the exception of NFSERR_WFLUSH which
 * no-one uses anyway), so we can happily mix code as long as we make sure
 * no NFSv3 errors are returned to NFSv2 clients.
 * Error codes that have a `--' in the v2 column are not part of the
 * standard, but seem to be widely used nevertheless.
 */
 enum nfs_stat {
	NFS_OK = 0,			/* v2 v3 v4 */
	NFSERR_PERM = 1,		/* v2 v3 v4 */
	NFSERR_NOENT = 2,		/* v2 v3 v4 */
	NFSERR_IO = 5,			/* v2 v3 v4 */
	NFSERR_NXIO = 6,		/* v2 v3 v4 */
	NFSERR_EAGAIN = 11,		/* v2 v3 */
	NFSERR_ACCES = 13,		/* v2 v3 v4 */
	NFSERR_EXIST = 17,		/* v2 v3 v4 */
	NFSERR_XDEV = 18,		/*    v3 v4 */
	NFSERR_NODEV = 19,		/* v2 v3 v4 */
	NFSERR_NOTDIR = 20,		/* v2 v3 v4 */
	NFSERR_ISDIR = 21,		/* v2 v3 v4 */
	NFSERR_INVAL = 22,		/* v2 v3 v4 */
	NFSERR_FBIG = 27,		/* v2 v3 v4 */
	NFSERR_NOSPC = 28,		/* v2 v3 v4 */
	NFSERR_ROFS = 30,		/* v2 v3 v4 */
	NFSERR_MLINK = 31,		/*    v3 v4 */
	NFSERR_OPNOTSUPP = 45,		/* v2 v3 */
	NFSERR_NAMETOOLONG = 63,	/* v2 v3 v4 */
	NFSERR_NOTEMPTY = 66,		/* v2 v3 v4 */
	NFSERR_DQUOT = 69,		/* v2 v3 v4 */
	NFSERR_STALE = 70,		/* v2 v3 v4 */
	NFSERR_REMOTE = 71,		/* v2 v3 */
	NFSERR_WFLUSH = 99,		/* v2    */
	NFSERR_BADHANDLE = 10001,	/*    v3 v4 */
	NFSERR_NOT_SYNC = 10002,	/*    v3 */
	NFSERR_BAD_COOKIE = 10003,	/*    v3 v4 */
	NFSERR_NOTSUPP = 10004,		/*    v3 v4 */
	NFSERR_TOOSMALL = 10005,	/*    v3 v4 */
	NFSERR_SERVERFAULT = 10006,	/*    v3 v4 */
	NFSERR_BADTYPE = 10007,		/*    v3 v4 */
	NFSERR_JUKEBOX = 10008,		/*    v3 v4 */
	NFSERR_SAME = 10009,		/*       v4 */
	NFSERR_DENIED = 10010,		/*       v4 */
	NFSERR_EXPIRED = 10011,		/*       v4 */
	NFSERR_LOCKED = 10012,		/*       v4 */
	NFSERR_GRACE = 10013,		/*       v4 */
	NFSERR_FHEXPIRED = 10014,	/*       v4 */
	NFSERR_SHARE_DENIED = 10015,	/*       v4 */
	NFSERR_WRONGSEC = 10016,	/*       v4 */
	NFSERR_CLID_INUSE = 10017,	/*       v4 */
	NFSERR_RESOURCE = 10018,	/*       v4 */
	NFSERR_MOVED = 10019,		/*       v4 */
	NFSERR_NOFILEHANDLE = 10020,	/*       v4 */
	NFSERR_MINOR_VERS_MISMATCH = 10021,   /* v4 */
	NFSERR_STALE_CLIENTID = 10022,	/*       v4 */
	NFSERR_STALE_STATEID = 10023,   /*       v4 */
	NFSERR_OLD_STATEID = 10024,     /*       v4 */
	NFSERR_BAD_STATEID = 10025,     /*       v4 */  
	NFSERR_BAD_SEQID = 10026,	/*       v4 */
	NFSERR_NOT_SAME = 10027,	/*       v4 */
	NFSERR_LOCK_RANGE = 10028,	/*       v4 */
	NFSERR_SYMLINK = 10029,		/*       v4 */
	NFSERR_RESTOREFH = 10030,	/*       v4 */
	NFSERR_LEASE_MOVED = 10031,	/*       v4 */
	NFSERR_ATTRNOTSUPP = 10032,	/*       v4 */
	NFSERR_NO_GRACE = 10033,	/*       v4 */
	NFSERR_RECLAIM_BAD = 10034,	/*       v4 */
	NFSERR_RECLAIM_CONFLICT = 10035,/*       v4 */
	NFSERR_BAD_XDR = 10036,		/*       v4 */
	NFSERR_LOCKS_HELD = 10037,	/*       v4 */
	NFSERR_OPENMODE = 10038,       /*       v4 */
	NFSERR_BADOWNER = 10039,       /*       v4 */
	NFSERR_BADCHAR = 10040,        /*       v4 */
	NFSERR_BADNAME = 10041,        /*       v4 */
	NFSERR_BAD_RANGE = 10042,      /*       v4 */
	NFSERR_LOCK_NOTSUPP = 10043,   /*       v4 */
	NFSERR_OP_ILLEGAL = 10044,     /*       v4 */
	NFSERR_DEADLOCK = 10045,       /*       v4 */
	NFSERR_FILE_OPEN = 10046,      /*       v4 */
	NFSERR_ADMIN_REVOKED = 10047,  /*       v4 */
	NFSERR_CB_PATH_DOWN = 10048,   /*       v4 */
};

/* NFSv2 file types - beware, these are not the same in NFSv3 */

enum nfs_ftype {
	NFNON = 0,
	NFREG = 1,
	NFDIR = 2,
	NFBLK = 3,
	NFCHR = 4,
	NFLNK = 5,
	NFSOCK = 6,
	NFBAD = 7,
	NFFIFO = 8
};

#ifdef __KERNEL__
#include <linux/sunrpc/msg_prot.h>
#include <linux/string.h>

/*
 * This is the kernel NFS client file handle representation
 */
#define NFS_MAXFHSIZE		128
struct nfs_fh {
	unsigned short		size;
	unsigned char		data[NFS_MAXFHSIZE];
};

/*
 * Returns a zero iff the size and data fields match.
 * Checks only "size" bytes in the data field.
 */
static inline int nfs_compare_fh(const struct nfs_fh *a, const struct nfs_fh *b)
{
	return a->size != b->size || memcmp(a->data, b->data, a->size) != 0;
}

static inline void nfs_copy_fh(struct nfs_fh *target, const struct nfs_fh *source)
{
	target->size = source->size;
	memcpy(target->data, source->data, source->size);
}


/*
 * This is really a general kernel constant, but since nothing like
 * this is defined in the kernel headers, I have to do it here.
 */
#define NFS_OFFSET_MAX		((__s64)((~(__u64)0) >> 1))


enum nfs3_stable_how {
	NFS_UNSTABLE = 0,
	NFS_DATA_SYNC = 1,
	NFS_FILE_SYNC = 2
};
#endif /* __KERNEL__ */
#endif /* _LINUX_NFS_H */
