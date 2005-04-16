/*
 *  smb_fs.h
 *
 *  Copyright (C) 1995 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 */

#ifndef _LINUX_SMB_FS_H
#define _LINUX_SMB_FS_H

#include <linux/smb.h>
#include <linux/smb_fs_i.h>
#include <linux/smb_fs_sb.h>

/*
 * ioctl commands
 */
#define	SMB_IOC_GETMOUNTUID		_IOR('u', 1, __kernel_old_uid_t)
#define SMB_IOC_NEWCONN                 _IOW('u', 2, struct smb_conn_opt)

/* __kernel_uid_t can never change, so we have to use __kernel_uid32_t */
#define	SMB_IOC_GETMOUNTUID32		_IOR('u', 3, __kernel_uid32_t)


#ifdef __KERNEL__

#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/smb_mount.h>
#include <asm/unaligned.h>

static inline struct smb_sb_info *SMB_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct smb_inode_info *SMB_I(struct inode *inode)
{
	return container_of(inode, struct smb_inode_info, vfs_inode);
}

/* macro names are short for word, double-word, long value (?) */
#define WVAL(buf,pos) \
	(le16_to_cpu(get_unaligned((u16 *)((u8 *)(buf) + (pos)))))
#define DVAL(buf,pos) \
	(le32_to_cpu(get_unaligned((u32 *)((u8 *)(buf) + (pos)))))
#define LVAL(buf,pos) \
	(le64_to_cpu(get_unaligned((u64 *)((u8 *)(buf) + (pos)))))
#define WSET(buf,pos,val) \
	put_unaligned(cpu_to_le16((u16)(val)), (u16 *)((u8 *)(buf) + (pos)))
#define DSET(buf,pos,val) \
	put_unaligned(cpu_to_le32((u32)(val)), (u32 *)((u8 *)(buf) + (pos)))
#define LSET(buf,pos,val) \
	put_unaligned(cpu_to_le64((u64)(val)), (u64 *)((u8 *)(buf) + (pos)))

/* where to find the base of the SMB packet proper */
#define smb_base(buf) ((u8 *)(((u8 *)(buf))+4))

#ifdef DEBUG_SMB_MALLOC

#include <linux/slab.h>

extern int smb_malloced;
extern int smb_current_vmalloced;
extern int smb_current_kmalloced;

static inline void *
smb_vmalloc(unsigned int size)
{
        smb_malloced += 1;
        smb_current_vmalloced += 1;
        return vmalloc(size);
}

static inline void
smb_vfree(void *obj)
{
        smb_current_vmalloced -= 1;
        vfree(obj);
}

static inline void *
smb_kmalloc(size_t size, int flags)
{
	smb_malloced += 1;
	smb_current_kmalloced += 1;
	return kmalloc(size, flags);
}

static inline void
smb_kfree(void *obj)
{
	smb_current_kmalloced -= 1;
	kfree(obj);
}

#else /* DEBUG_SMB_MALLOC */

#define smb_kmalloc(s,p)	kmalloc(s,p)
#define smb_kfree(o)		kfree(o)
#define smb_vmalloc(s)		vmalloc(s)
#define smb_vfree(o)		vfree(o)

#endif /* DEBUG_SMB_MALLOC */

/*
 * Flags for the in-memory inode
 */
#define SMB_F_LOCALWRITE	0x02	/* file modified locally */


/* NT1 protocol capability bits */
#define SMB_CAP_RAW_MODE         0x00000001
#define SMB_CAP_MPX_MODE         0x00000002
#define SMB_CAP_UNICODE          0x00000004
#define SMB_CAP_LARGE_FILES      0x00000008
#define SMB_CAP_NT_SMBS          0x00000010
#define SMB_CAP_RPC_REMOTE_APIS  0x00000020
#define SMB_CAP_STATUS32         0x00000040
#define SMB_CAP_LEVEL_II_OPLOCKS 0x00000080
#define SMB_CAP_LOCK_AND_READ    0x00000100
#define SMB_CAP_NT_FIND          0x00000200
#define SMB_CAP_DFS              0x00001000
#define SMB_CAP_LARGE_READX      0x00004000
#define SMB_CAP_LARGE_WRITEX     0x00008000
#define SMB_CAP_UNIX             0x00800000	/* unofficial ... */


/*
 * This is the time we allow an inode, dentry or dir cache to live. It is bad
 * for performance to have shorter ttl on an inode than on the cache. It can
 * cause refresh on each inode for a dir listing ... one-by-one
 */
#define SMB_MAX_AGE(server) (((server)->mnt->ttl * HZ) / 1000)

static inline void
smb_age_dentry(struct smb_sb_info *server, struct dentry *dentry)
{
	dentry->d_time = jiffies - SMB_MAX_AGE(server);
}

struct smb_cache_head {
	time_t		mtime;	/* unused */
	unsigned long	time;	/* cache age */
	unsigned long	end;	/* last valid fpos in cache */
	int		eof;
};

#define SMB_DIRCACHE_SIZE	((int)(PAGE_CACHE_SIZE/sizeof(struct dentry *)))
union smb_dir_cache {
	struct smb_cache_head   head;
	struct dentry           *dentry[SMB_DIRCACHE_SIZE];
};

#define SMB_FIRSTCACHE_SIZE	((int)((SMB_DIRCACHE_SIZE * \
	sizeof(struct dentry *) - sizeof(struct smb_cache_head)) / \
	sizeof(struct dentry *)))

#define SMB_DIRCACHE_START      (SMB_DIRCACHE_SIZE - SMB_FIRSTCACHE_SIZE)

struct smb_cache_control {
	struct  smb_cache_head		head;
	struct  page			*page;
	union   smb_dir_cache		*cache;
	unsigned long			fpos, ofs;
	int				filled, valid, idx;
};

#define SMB_OPS_NUM_STATIC	5
struct smb_ops {
	int (*read)(struct inode *inode, loff_t offset, int count,
		    char *data);
	int (*write)(struct inode *inode, loff_t offset, int count, const
		     char *data);
	int (*readdir)(struct file *filp, void *dirent, filldir_t filldir,
		       struct smb_cache_control *ctl);

	int (*getattr)(struct smb_sb_info *server, struct dentry *dir,
		       struct smb_fattr *fattr);
	/* int (*setattr)(...); */      /* setattr is really icky! */

	int (*truncate)(struct inode *inode, loff_t length);


	/* --- --- --- end of "static" entries --- --- --- */

	int (*convert)(unsigned char *output, int olen,
		       const unsigned char *input, int ilen,
		       struct nls_table *nls_from,
		       struct nls_table *nls_to);
};

static inline int
smb_is_open(struct inode *i)
{
	return (SMB_I(i)->open == server_from_inode(i)->generation);
}

extern void smb_install_null_ops(struct smb_ops *);
#endif /* __KERNEL__ */

#endif /* _LINUX_SMB_FS_H */
