/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1995, 1996, 1997 Olaf Kirch <okir@monad.swb.de>
 *
 * This file describes the layout of the file handles as passed
 * over the wire.
 */
#ifndef _LINUX_NFSD_NFSFH_H
#define _LINUX_NFSD_NFSFH_H

#include <linux/crc32.h>
#include <linux/sunrpc/svc.h>
#include <linux/iversion.h>
#include <linux/exportfs.h>
#include <linux/nfs4.h>

/*
 * The file handle starts with a sequence of four-byte words.
 * The first word contains a version number (1) and three descriptor bytes
 * that tell how the remaining 3 variable length fields should be handled.
 * These three bytes are auth_type, fsid_type and fileid_type.
 *
 * All four-byte values are in host-byte-order.
 *
 * The auth_type field is deprecated and must be set to 0.
 *
 * The fsid_type identifies how the filesystem (or export point) is
 *    encoded.
 *  Current values:
 *     0  - 4 byte device id (ms-2-bytes major, ls-2-bytes minor), 4byte inode number
 *        NOTE: we cannot use the kdev_t device id value, because kdev_t.h
 *              says we mustn't.  We must break it up and reassemble.
 *     1  - 4 byte user specified identifier
 *     2  - 4 byte major, 4 byte minor, 4 byte inode number - DEPRECATED
 *     3  - 4 byte device id, encoded for user-space, 4 byte inode number
 *     4  - 4 byte inode number and 4 byte uuid
 *     5  - 8 byte uuid
 *     6  - 16 byte uuid
 *     7  - 8 byte inode number and 16 byte uuid
 *
 * The fileid_type identifies how the file within the filesystem is encoded.
 *   The values for this field are filesystem specific, exccept that
 *   filesystems must not use the values '0' or '0xff'. 'See enum fid_type'
 *   in include/linux/exportfs.h for currently registered values.
 */

struct knfsd_fh {
	unsigned int	fh_size;	/*
					 * Points to the current size while
					 * building a new file handle.
					 */
	union {
		char			fh_raw[NFS4_FHSIZE];
		struct {
			u8		fh_version;	/* == 1 */
			u8		fh_auth_type;	/* deprecated */
			u8		fh_fsid_type;
			u8		fh_fileid_type;
			u32		fh_fsid[]; /* flexible-array member */
		};
	};
};

static inline __u32 ino_t_to_u32(ino_t ino)
{
	return (__u32) ino;
}

static inline ino_t u32_to_ino_t(__u32 uino)
{
	return (ino_t) uino;
}

/*
 * This is the internal representation of an NFS handle used in knfsd.
 * pre_mtime/post_version will be used to support wcc_attr's in NFSv3.
 */
typedef struct svc_fh {
	struct knfsd_fh		fh_handle;	/* FH data */
	int			fh_maxsize;	/* max size for fh_handle */
	struct dentry *		fh_dentry;	/* validated dentry */
	struct svc_export *	fh_export;	/* export pointer */

	bool			fh_locked;	/* inode locked by us */
	bool			fh_want_write;	/* remount protection taken */
	bool			fh_no_wcc;	/* no wcc data needed */
	bool			fh_no_atomic_attr;
						/*
						 * wcc data is not atomic with
						 * operation
						 */
	int			fh_flags;	/* FH flags */
#ifdef CONFIG_NFSD_V3
	bool			fh_post_saved;	/* post-op attrs saved */
	bool			fh_pre_saved;	/* pre-op attrs saved */

	/* Pre-op attributes saved during fh_lock */
	__u64			fh_pre_size;	/* size before operation */
	struct timespec64	fh_pre_mtime;	/* mtime before oper */
	struct timespec64	fh_pre_ctime;	/* ctime before oper */
	/*
	 * pre-op nfsv4 change attr: note must check IS_I_VERSION(inode)
	 *  to find out if it is valid.
	 */
	u64			fh_pre_change;

	/* Post-op attributes saved in fh_unlock */
	struct kstat		fh_post_attr;	/* full attrs after operation */
	u64			fh_post_change; /* nfsv4 change; see above */
#endif /* CONFIG_NFSD_V3 */
} svc_fh;
#define NFSD4_FH_FOREIGN (1<<0)
#define SET_FH_FLAG(c, f) ((c)->fh_flags |= (f))
#define HAS_FH_FLAG(c, f) ((c)->fh_flags & (f))

enum nfsd_fsid {
	FSID_DEV = 0,
	FSID_NUM,
	FSID_MAJOR_MINOR,
	FSID_ENCODE_DEV,
	FSID_UUID4_INUM,
	FSID_UUID8,
	FSID_UUID16,
	FSID_UUID16_INUM,
};

enum fsid_source {
	FSIDSOURCE_DEV,
	FSIDSOURCE_FSID,
	FSIDSOURCE_UUID,
};
extern enum fsid_source fsid_source(const struct svc_fh *fhp);


/*
 * This might look a little large to "inline" but in all calls except
 * one, 'vers' is constant so moste of the function disappears.
 *
 * In some cases the values are considered to be host endian and in
 * others, net endian. fsidv is always considered to be u32 as the
 * callers don't know which it will be. So we must use __force to keep
 * sparse from complaining. Since these values are opaque to the
 * client, that shouldn't be a problem.
 */
static inline void mk_fsid(int vers, u32 *fsidv, dev_t dev, ino_t ino,
			   u32 fsid, unsigned char *uuid)
{
	u32 *up;
	switch(vers) {
	case FSID_DEV:
		fsidv[0] = (__force __u32)htonl((MAJOR(dev)<<16) |
				 MINOR(dev));
		fsidv[1] = ino_t_to_u32(ino);
		break;
	case FSID_NUM:
		fsidv[0] = fsid;
		break;
	case FSID_MAJOR_MINOR:
		fsidv[0] = (__force __u32)htonl(MAJOR(dev));
		fsidv[1] = (__force __u32)htonl(MINOR(dev));
		fsidv[2] = ino_t_to_u32(ino);
		break;

	case FSID_ENCODE_DEV:
		fsidv[0] = new_encode_dev(dev);
		fsidv[1] = ino_t_to_u32(ino);
		break;

	case FSID_UUID4_INUM:
		/* 4 byte fsid and inode number */
		up = (u32*)uuid;
		fsidv[0] = ino_t_to_u32(ino);
		fsidv[1] = up[0] ^ up[1] ^ up[2] ^ up[3];
		break;

	case FSID_UUID8:
		/* 8 byte fsid  */
		up = (u32*)uuid;
		fsidv[0] = up[0] ^ up[2];
		fsidv[1] = up[1] ^ up[3];
		break;

	case FSID_UUID16:
		/* 16 byte fsid - NFSv3+ only */
		memcpy(fsidv, uuid, 16);
		break;

	case FSID_UUID16_INUM:
		/* 8 byte inode and 16 byte fsid */
		*(u64*)fsidv = (u64)ino;
		memcpy(fsidv+2, uuid, 16);
		break;
	default: BUG();
	}
}

static inline int key_len(int type)
{
	switch(type) {
	case FSID_DEV:		return 8;
	case FSID_NUM: 		return 4;
	case FSID_MAJOR_MINOR:	return 12;
	case FSID_ENCODE_DEV:	return 8;
	case FSID_UUID4_INUM:	return 8;
	case FSID_UUID8:	return 8;
	case FSID_UUID16:	return 16;
	case FSID_UUID16_INUM:	return 24;
	default: return 0;
	}
}

/*
 * Shorthand for dprintk()'s
 */
extern char * SVCFH_fmt(struct svc_fh *fhp);

/*
 * Function prototypes
 */
__be32	fh_verify(struct svc_rqst *, struct svc_fh *, umode_t, int);
__be32	fh_compose(struct svc_fh *, struct svc_export *, struct dentry *, struct svc_fh *);
__be32	fh_update(struct svc_fh *);
void	fh_put(struct svc_fh *);

static __inline__ struct svc_fh *
fh_copy(struct svc_fh *dst, struct svc_fh *src)
{
	WARN_ON(src->fh_dentry || src->fh_locked);
			
	*dst = *src;
	return dst;
}

static inline void
fh_copy_shallow(struct knfsd_fh *dst, struct knfsd_fh *src)
{
	dst->fh_size = src->fh_size;
	memcpy(&dst->fh_raw, &src->fh_raw, src->fh_size);
}

static __inline__ struct svc_fh *
fh_init(struct svc_fh *fhp, int maxsize)
{
	memset(fhp, 0, sizeof(*fhp));
	fhp->fh_maxsize = maxsize;
	return fhp;
}

static inline bool fh_match(struct knfsd_fh *fh1, struct knfsd_fh *fh2)
{
	if (fh1->fh_size != fh2->fh_size)
		return false;
	if (memcmp(fh1->fh_raw, fh2->fh_raw, fh1->fh_size) != 0)
		return false;
	return true;
}

static inline bool fh_fsid_match(struct knfsd_fh *fh1, struct knfsd_fh *fh2)
{
	if (fh1->fh_fsid_type != fh2->fh_fsid_type)
		return false;
	if (memcmp(fh1->fh_fsid, fh2->fh_fsid, key_len(fh1->fh_fsid_type)) != 0)
		return false;
	return true;
}

#ifdef CONFIG_CRC32
/**
 * knfsd_fh_hash - calculate the crc32 hash for the filehandle
 * @fh - pointer to filehandle
 *
 * returns a crc32 hash for the filehandle that is compatible with
 * the one displayed by "wireshark".
 */
static inline u32 knfsd_fh_hash(const struct knfsd_fh *fh)
{
	return ~crc32_le(0xFFFFFFFF, fh->fh_raw, fh->fh_size);
}
#else
static inline u32 knfsd_fh_hash(const struct knfsd_fh *fh)
{
	return 0;
}
#endif

#ifdef CONFIG_NFSD_V3
/*
 * The wcc data stored in current_fh should be cleared
 * between compound ops.
 */
static inline void
fh_clear_wcc(struct svc_fh *fhp)
{
	fhp->fh_post_saved = false;
	fhp->fh_pre_saved = false;
}

/*
 * We could use i_version alone as the change attribute.  However,
 * i_version can go backwards after a reboot.  On its own that doesn't
 * necessarily cause a problem, but if i_version goes backwards and then
 * is incremented again it could reuse a value that was previously used
 * before boot, and a client who queried the two values might
 * incorrectly assume nothing changed.
 *
 * By using both ctime and the i_version counter we guarantee that as
 * long as time doesn't go backwards we never reuse an old value.
 */
static inline u64 nfsd4_change_attribute(struct kstat *stat,
					 struct inode *inode)
{
	if (inode->i_sb->s_export_op->fetch_iversion)
		return inode->i_sb->s_export_op->fetch_iversion(inode);
	else if (IS_I_VERSION(inode)) {
		u64 chattr;

		chattr =  stat->ctime.tv_sec;
		chattr <<= 30;
		chattr += stat->ctime.tv_nsec;
		chattr += inode_query_iversion(inode);
		return chattr;
	} else
		return time_to_chattr(&stat->ctime);
}

extern void fill_pre_wcc(struct svc_fh *fhp);
extern void fill_post_wcc(struct svc_fh *fhp);
#else
#define fh_clear_wcc(ignored)
#define fill_pre_wcc(ignored)
#define fill_post_wcc(notused)
#endif /* CONFIG_NFSD_V3 */


/*
 * Lock a file handle/inode
 * NOTE: both fh_lock and fh_unlock are done "by hand" in
 * vfs.c:nfsd_rename as it needs to grab 2 i_mutex's at once
 * so, any changes here should be reflected there.
 */

static inline void
fh_lock_nested(struct svc_fh *fhp, unsigned int subclass)
{
	struct dentry	*dentry = fhp->fh_dentry;
	struct inode	*inode;

	BUG_ON(!dentry);

	if (fhp->fh_locked) {
		printk(KERN_WARNING "fh_lock: %pd2 already locked!\n",
			dentry);
		return;
	}

	inode = d_inode(dentry);
	inode_lock_nested(inode, subclass);
	fill_pre_wcc(fhp);
	fhp->fh_locked = true;
}

static inline void
fh_lock(struct svc_fh *fhp)
{
	fh_lock_nested(fhp, I_MUTEX_NORMAL);
}

/*
 * Unlock a file handle/inode
 */
static inline void
fh_unlock(struct svc_fh *fhp)
{
	if (fhp->fh_locked) {
		fill_post_wcc(fhp);
		inode_unlock(d_inode(fhp->fh_dentry));
		fhp->fh_locked = false;
	}
}

#endif /* _LINUX_NFSD_NFSFH_H */
