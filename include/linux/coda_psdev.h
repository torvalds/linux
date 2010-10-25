#ifndef __CODA_PSDEV_H
#define __CODA_PSDEV_H

#include <linux/magic.h>

#define CODA_PSDEV_MAJOR 67
#define MAX_CODADEVS  5	   /* how many do we allow */

#ifdef __KERNEL__
#include <linux/backing-dev.h>
#include <linux/mutex.h>

struct kstatfs;

/* communication pending/processing queues */
struct venus_comm {
	u_long		    vc_seq;
	wait_queue_head_t   vc_waitq; /* Venus wait queue */
	struct list_head    vc_pending;
	struct list_head    vc_processing;
	int                 vc_inuse;
	struct super_block *vc_sb;
	struct backing_dev_info bdi;
	struct mutex	    vc_mutex;
};


static inline struct venus_comm *coda_vcp(struct super_block *sb)
{
	return (struct venus_comm *)((sb)->s_fs_info);
}

/* upcalls */
int venus_rootfid(struct super_block *sb, struct CodaFid *fidp);
int venus_getattr(struct super_block *sb, struct CodaFid *fid,
		  struct coda_vattr *attr);
int venus_setattr(struct super_block *, struct CodaFid *, struct coda_vattr *);
int venus_lookup(struct super_block *sb, struct CodaFid *fid, 
		 const char *name, int length, int *type, 
		 struct CodaFid *resfid);
int venus_close(struct super_block *sb, struct CodaFid *fid, int flags,
		vuid_t uid);
int venus_open(struct super_block *sb, struct CodaFid *fid, int flags,
	       struct file **f);
int venus_mkdir(struct super_block *sb, struct CodaFid *dirfid, 
		const char *name, int length, 
		struct CodaFid *newfid, struct coda_vattr *attrs);
int venus_create(struct super_block *sb, struct CodaFid *dirfid, 
		 const char *name, int length, int excl, int mode,
		 struct CodaFid *newfid, struct coda_vattr *attrs) ;
int venus_rmdir(struct super_block *sb, struct CodaFid *dirfid, 
		const char *name, int length);
int venus_remove(struct super_block *sb, struct CodaFid *dirfid, 
		 const char *name, int length);
int venus_readlink(struct super_block *sb, struct CodaFid *fid, 
		   char *buffer, int *length);
int venus_rename(struct super_block *, struct CodaFid *new_fid, 
		 struct CodaFid *old_fid, size_t old_length, 
		 size_t new_length, const char *old_name, 
		 const char *new_name);
int venus_link(struct super_block *sb, struct CodaFid *fid, 
		  struct CodaFid *dirfid, const char *name, int len );
int venus_symlink(struct super_block *sb, struct CodaFid *fid,
		  const char *name, int len, const char *symname, int symlen);
int venus_access(struct super_block *sb, struct CodaFid *fid, int mask);
int venus_pioctl(struct super_block *sb, struct CodaFid *fid,
		 unsigned int cmd, struct PioctlData *data);
int coda_downcall(struct venus_comm *vcp, int opcode, union outputArgs *out);
int venus_fsync(struct super_block *sb, struct CodaFid *fid);
int venus_statfs(struct dentry *dentry, struct kstatfs *sfs);

/*
 * Statistics
 */

extern struct venus_comm coda_comms[];
#endif /* __KERNEL__ */

/* messages between coda filesystem in kernel and Venus */
struct upc_req {
	struct list_head    uc_chain;
	caddr_t	            uc_data;
	u_short	            uc_flags;
	u_short             uc_inSize;  /* Size is at most 5000 bytes */
	u_short	            uc_outSize;
	u_short	            uc_opcode;  /* copied from data to save lookup */
	int		    uc_unique;
	wait_queue_head_t   uc_sleep;   /* process' wait queue */
};

#define CODA_REQ_ASYNC  0x1
#define CODA_REQ_READ   0x2
#define CODA_REQ_WRITE  0x4
#define CODA_REQ_ABORT  0x8

#endif
