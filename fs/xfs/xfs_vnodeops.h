#ifndef _XFS_VNODEOPS_H
#define _XFS_VNODEOPS_H 1

struct attrlist_cursor_kern;
struct bhv_vattr;
struct cred;
struct file;
struct inode;
struct iovec;
struct kiocb;
struct pipe_inode_info;
struct uio;
struct xfs_inode;
struct xfs_iomap;


int xfs_open(struct xfs_inode *ip);
int xfs_getattr(struct xfs_inode *ip, struct bhv_vattr *vap, int flags);
int xfs_setattr(struct xfs_inode *ip, struct bhv_vattr *vap, int flags,
		struct cred *credp);
int xfs_access(struct xfs_inode *ip, int mode, struct cred *credp);
int xfs_readlink(struct xfs_inode *ip, char *link);
int xfs_fsync(struct xfs_inode *ip, int flag, xfs_off_t start,
		xfs_off_t stop);
int xfs_release(struct xfs_inode *ip);
int xfs_inactive(struct xfs_inode *ip);
int xfs_lookup(struct xfs_inode *dp, bhv_vname_t *dentry,
		bhv_vnode_t **vpp);
int xfs_create(struct xfs_inode *dp, bhv_vname_t *dentry, mode_t mode,
		xfs_dev_t rdev, bhv_vnode_t **vpp, struct cred *credp);
int xfs_remove(struct xfs_inode *dp, bhv_vname_t	*dentry);
int xfs_link(struct xfs_inode *tdp, bhv_vnode_t *src_vp,
		bhv_vname_t *dentry);
int xfs_mkdir(struct xfs_inode *dp, bhv_vname_t *dentry,
		mode_t mode, bhv_vnode_t **vpp, struct cred *credp);
int xfs_rmdir(struct xfs_inode *dp, bhv_vname_t *dentry);
int xfs_readdir(struct xfs_inode	*dp, void *dirent, size_t bufsize,
		       xfs_off_t *offset, filldir_t filldir);
int xfs_symlink(struct xfs_inode *dp, bhv_vname_t *dentry,
		char *target_path, mode_t mode, bhv_vnode_t **vpp,
		struct cred *credp);
int xfs_fid2(struct xfs_inode *ip, fid_t	*fidp);
int xfs_rwlock(struct xfs_inode *ip, bhv_vrwlock_t locktype);
void xfs_rwunlock(struct xfs_inode *ip, bhv_vrwlock_t locktype);
int xfs_inode_flush(struct xfs_inode *ip, int flags);
int xfs_set_dmattrs(struct xfs_inode *ip, u_int evmask, u_int16_t state);
int xfs_reclaim(struct xfs_inode *ip);
int xfs_change_file_space(struct xfs_inode *ip, int cmd,
		xfs_flock64_t *bf, xfs_off_t offset,
		struct cred *credp, int	attr_flags);
int xfs_rename(struct xfs_inode *src_dp, bhv_vname_t *src_vname,
		bhv_vnode_t *target_dir_vp, bhv_vname_t *target_vname);
int xfs_attr_get(struct xfs_inode *ip, const char *name, char *value,
		int *valuelenp, int flags, cred_t *cred);
int xfs_attr_set(struct xfs_inode *dp, const char *name, char *value,
		int valuelen, int flags);
int xfs_attr_remove(struct xfs_inode *dp, const char *name, int flags);
int xfs_attr_list(struct xfs_inode *dp, char *buffer, int bufsize,
		int flags, struct attrlist_cursor_kern *cursor);
int xfs_ioctl(struct xfs_inode *ip, struct file *filp,
		int ioflags, unsigned int cmd, void __user *arg);
ssize_t xfs_read(struct xfs_inode *ip, struct kiocb *iocb,
		const struct iovec *iovp, unsigned int segs,
		loff_t *offset, int ioflags);
ssize_t xfs_sendfile(struct xfs_inode *ip, struct file *filp,
		loff_t *offset, int ioflags, size_t count,
		read_actor_t actor, void *target);
ssize_t xfs_splice_read(struct xfs_inode *ip, struct file *infilp,
		loff_t *ppos, struct pipe_inode_info *pipe, size_t count,
		int flags, int ioflags);
ssize_t xfs_splice_write(struct xfs_inode *ip,
		struct pipe_inode_info *pipe, struct file *outfilp,
		loff_t *ppos, size_t count, int flags, int ioflags);
ssize_t xfs_write(struct xfs_inode *xip, struct kiocb *iocb,
		const struct iovec *iovp, unsigned int nsegs,
		loff_t *offset, int ioflags);
int xfs_bmap(struct xfs_inode *ip, xfs_off_t offset, ssize_t count,
		int flags, struct xfs_iomap *iomapp, int *niomaps);
void xfs_tosspages(struct xfs_inode *inode, xfs_off_t first,
		xfs_off_t last, int fiopt);
int xfs_flushinval_pages(struct xfs_inode *ip, xfs_off_t first,
		xfs_off_t last, int fiopt);
int xfs_flush_pages(struct xfs_inode *ip, xfs_off_t first,
		xfs_off_t last, uint64_t flags, int fiopt);

#endif /* _XFS_VNODEOPS_H */
