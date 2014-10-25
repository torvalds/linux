/*
 * Copyright (C) 1995, 1996, 1997 Olaf Kirch <okir@monad.swb.de>
 *
 * This file describes the layout of the file handles as passed
 * over the wire.
 */
#ifndef _LINUX_NFSD_NFSFH_H
#define _LINUX_NFSD_NFSFH_H

#include <linux/sunrpc/svc.h>
#include <uapi/linux/nfsd/nfsfh.h>

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
	unsigned char		fh_want_write;	/* remount protection taken */

#ifdef CONFIG_NFSD_V3
	unsigned char		fh_post_saved;	/* post-op attrs saved */
	unsigned char		fh_pre_saved;	/* pre-op attrs saved */

	/* Pre-op attributes saved during fh_lock */
	__u64			fh_pre_size;	/* size before operation */
	struct timespec		fh_pre_mtime;	/* mtime before oper */
	struct timespec		fh_pre_ctime;	/* ctime before oper */
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
 * The wcc data stored in current_fh should be cleared
 * between compound ops.
 */
static inline void
fh_clear_wcc(struct svc_fh *fhp)
{
	fhp->fh_post_saved = 0;
	fhp->fh_pre_saved = 0;
}

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
		fhp->fh_pre_change = inode->i_version;
		fhp->fh_pre_saved = 1;
	}
}

extern void fill_post_wcc(struct svc_fh *);
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
	if (fhp->fh_locked) {
		fill_post_wcc(fhp);
		mutex_unlock(&fhp->fh_dentry->d_inode->i_mutex);
		fhp->fh_locked = 0;
	}
}

#endif /* _LINUX_NFSD_NFSFH_H */
