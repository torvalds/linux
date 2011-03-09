/* Copyright (C) 1995, 1996, 1997 Olaf Kirch <okir@monad.swb.de> */

#ifndef _LINUX_NFSD_FH_INT_H
#define _LINUX_NFSD_FH_INT_H

#include <linux/nfsd/nfsfh.h>

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
		fhp->fh_pre_change = inode->i_version;
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
	if (fhp->fh_locked) {
		fill_post_wcc(fhp);
		mutex_unlock(&fhp->fh_dentry->d_inode->i_mutex);
		fhp->fh_locked = 0;
	}
}

#endif /* _LINUX_NFSD_FH_INT_H */
