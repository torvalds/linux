/*
 * include/linux/nfsd/nfsfh.h
 *
 * This file describes the layout of the file handles as passed
 * over the wire.
 *
 * Earlier versions of knfsd used to sign file handles using keyed MD5
 * or SHA. I've removed this code, because it doesn't give you more
 * security than blocking external access to port 2049 on your firewall.
 *
 * Copyright (C) 1995, 1996, 1997 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_NFSD_FH_H
#define _LINUX_NFSD_FH_H

# include <linux/types.h>
#ifdef __KERNEL__
# include <linux/string.h>
# include <linux/fs.h>
#endif
#include <linux/nfsd/const.h>
#include <linux/nfsd/debug.h>

/*
 * This is the old "dentry style" Linux NFSv2 file handle.
 *
 * The xino and xdev fields are currently used to transport the
 * ino/dev of the exported inode.
 */
struct nfs_fhbase_old {
	__u32		fb_dcookie;	/* dentry cookie - always 0xfeebbaca */
	__u32		fb_ino;		/* our inode number */
	__u32		fb_dirino;	/* dir inode number, 0 for directories */
	__u32		fb_dev;		/* our device */
	__u32		fb_xdev;
	__u32		fb_xino;
	__u32		fb_generation;
};

/*
 * This is the new flexible, extensible style NFSv2/v3 file handle.
 * by Neil Brown <neilb@cse.unsw.edu.au> - March 2000
 *
 * The file handle is seens as a list of 4byte words.
 * The first word contains a version number (1) and four descriptor bytes
 * that tell how the remaining 3 variable length fields should be handled.
 * These three bytes are auth_type, fsid_type and fileid_type.
 *
 * All 4byte values are in host-byte-order.
 *
 * The auth_type field specifies how the filehandle can be authenticated
 * This might allow a file to be confirmed to be in a writable part of a
 * filetree without checking the path from it upto the root.
 * Current values:
 *     0  - No authentication.  fb_auth is 0 bytes long
 * Possible future values:
 *     1  - 4 bytes taken from MD5 hash of the remainer of the file handle
 *          prefixed by a secret and with the important export flags.
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
 * The fileid_type identified how the file within the filesystem is encoded.
 * This is (will be) passed to, and set by, the underlying filesystem if it supports
 * filehandle operations.  The filesystem must not use the value '0' or '0xff' and may
 * only use the values 1 and 2 as defined below:
 *  Current values:
 *    0   - The root, or export point, of the filesystem.  fb_fileid is 0 bytes.
 *    1   - 32bit inode number, 32 bit generation number.
 *    2   - 32bit inode number, 32 bit generation number, 32 bit parent directory inode number.
 *
 */
struct nfs_fhbase_new {
	__u8		fb_version;	/* == 1, even => nfs_fhbase_old */
	__u8		fb_auth_type;
	__u8		fb_fsid_type;
	__u8		fb_fileid_type;
	__u32		fb_auth[1];
/*	__u32		fb_fsid[0]; floating */
/*	__u32		fb_fileid[0]; floating */
};

struct knfsd_fh {
	unsigned int	fh_size;	/* significant for NFSv3.
					 * Points to the current size while building
					 * a new file handle
					 */
	union {
		struct nfs_fhbase_old	fh_old;
		__u32			fh_pad[NFS4_FHSIZE/4];
		struct nfs_fhbase_new	fh_new;
	} fh_base;
};

#define ofh_dcookie		fh_base.fh_old.fb_dcookie
#define ofh_ino			fh_base.fh_old.fb_ino
#define ofh_dirino		fh_base.fh_old.fb_dirino
#define ofh_dev			fh_base.fh_old.fb_dev
#define ofh_xdev		fh_base.fh_old.fb_xdev
#define ofh_xino		fh_base.fh_old.fb_xino
#define ofh_generation		fh_base.fh_old.fb_generation

#define	fh_version		fh_base.fh_new.fb_version
#define	fh_fsid_type		fh_base.fh_new.fb_fsid_type
#define	fh_auth_type		fh_base.fh_new.fb_auth_type
#define	fh_fileid_type		fh_base.fh_new.fb_fileid_type
#define	fh_auth			fh_base.fh_new.fb_auth
#define	fh_fsid			fh_base.fh_new.fb_auth

#ifdef __KERNEL__

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
	struct dentry *		fh_dentry;	/* validated dentry */
	struct svc_export *	fh_export;	/* export pointer */
	int			fh_maxsize;	/* max size for fh_handle */

	unsigned char		fh_locked;	/* inode locked by us */

#ifdef CONFIG_NFSD_V3
	unsigned char		fh_post_saved;	/* post-op attrs saved */
	unsigned char		fh_pre_saved;	/* pre-op attrs saved */

	/* Pre-op attributes saved during fh_lock */
	__u64			fh_pre_size;	/* size before operation */
	struct timespec		fh_pre_mtime;	/* mtime before oper */
	struct timespec		fh_pre_ctime;	/* ctime before oper */

	/* Post-op attributes saved in fh_unlock */
	struct kstat		fh_post_attr;	/* full attrs after operation */
#endif /* CONFIG_NFSD_V3 */

} svc_fh;

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
extern enum fsid_source fsid_source(struct svc_fh *fhp);


/* This might look a little large to "inline" but in all calls except
 * one, 'vers' is constant so moste of the function disappears.
 */
static inline void mk_fsid(int vers, u32 *fsidv, dev_t dev, ino_t ino,
			   u32 fsid, unsigned char *uuid)
{
	u32 *up;
	switch(vers) {
	case FSID_DEV:
		fsidv[0] = htonl((MAJOR(dev)<<16) |
				 MINOR(dev));
		fsidv[1] = ino_t_to_u32(ino);
		break;
	case FSID_NUM:
		fsidv[0] = fsid;
		break;
	case FSID_MAJOR_MINOR:
		fsidv[0] = htonl(MAJOR(dev));
		fsidv[1] = htonl(MINOR(dev));
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
__be32	fh_verify(struct svc_rqst *, struct svc_fh *, int, int);
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
	memcpy(&dst->fh_base, &src->fh_base, src->fh_size);
}

static __inline__ struct svc_fh *
fh_init(struct svc_fh *fhp, int maxsize)
{
	memset(fhp, 0, sizeof(*fhp));
	fhp->fh_maxsize = maxsize;
	return fhp;
}

#ifdef CONFIG_NFSD_V3
/*
 * Fill in the pre_op attr for the wcc data
 */
static inline void
fill_pre_wcc(struct svc_fh *fhp)
{
	struct inode    *inode;

	inode = fhp->fh_dentry->d_inode;
	if (!fhp->fh_pre_saved) {
		fhp->fh_pre_mtime = inode->i_mtime;
		fhp->fh_pre_ctime = inode->i_ctime;
		fhp->fh_pre_size  = inode->i_size;
		fhp->fh_pre_saved = 1;
	}
}

extern void fill_post_wcc(struct svc_fh *);
#else
#define	fill_pre_wcc(ignored)
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

	dfprintk(FILEOP, "nfsd: fh_lock(%s) locked = %d\n",
			SVCFH_fmt(fhp), fhp->fh_locked);

	BUG_ON(!dentry);

	if (fhp->fh_locked) {
		printk(KERN_WARNING "fh_lock: %s/%s already locked!\n",
			dentry->d_parent->d_name.name, dentry->d_name.name);
		return;
	}

	inode = dentry->d_inode;
	mutex_lock_nested(&inode->i_mutex, subclass);
	fill_pre_wcc(fhp);
	fhp->fh_locked = 1;
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
	BUG_ON(!fhp->fh_dentry);

	if (fhp->fh_locked) {
		fill_post_wcc(fhp);
		mutex_unlock(&fhp->fh_dentry->d_inode->i_mutex);
		fhp->fh_locked = 0;
	}
}
#endif /* __KERNEL__ */


#endif /* _LINUX_NFSD_FH_H */
