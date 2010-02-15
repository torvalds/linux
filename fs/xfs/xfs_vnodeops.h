#ifndef _XFS_VNODEOPS_H
#define _XFS_VNODEOPS_H 1

struct attrlist_cursor_kern;
struct cred;
struct file;
struct iattr;
struct inode;
struct iovec;
struct kiocb;
struct pipe_inode_info;
struct uio;
struct xfs_inode;
struct xfs_iomap;


int xfs_setattr(struct xfs_inode *ip, struct iattr *vap, int flags);
#define	XFS_ATTR_DMI		0x01	/* invocation from a DMI function */
#define	XFS_ATTR_NONBLOCK	0x02	/* return EAGAIN if operation would block */
#define XFS_ATTR_NOLOCK		0x04	/* Don't grab any conflicting locks */
#define XFS_ATTR_NOACL		0x08	/* Don't call xfs_acl_chmod */

int xfs_readlink(struct xfs_inode *ip, char *link);
int xfs_release(struct xfs_inode *ip);
int xfs_inactive(struct xfs_inode *ip);
int xfs_lookup(struct xfs_inode *dp, struct xfs_name *name,
		struct xfs_inode **ipp, struct xfs_name *ci_name);
int xfs_create(struct xfs_inode *dp, struct xfs_name *name, mode_t mode,
		xfs_dev_t rdev, struct xfs_inode **ipp, cred_t *credp);
int xfs_remove(struct xfs_inode *dp, struct xfs_name *name,
		struct xfs_inode *ip);
int xfs_link(struct xfs_inode *tdp, struct xfs_inode *sip,
		struct xfs_name *target_name);
int xfs_readdir(struct xfs_inode	*dp, void *dirent, size_t bufsize,
		       xfs_off_t *offset, filldir_t filldir);
int xfs_symlink(struct xfs_inode *dp, struct xfs_name *link_name,
		const char *target_path, mode_t mode, struct xfs_inode **ipp,
		cred_t *credp);
int xfs_set_dmattrs(struct xfs_inode *ip, u_int evmask, u_int16_t state);
int xfs_change_file_space(struct xfs_inode *ip, int cmd,
		xfs_flock64_t *bf, xfs_off_t offset, int attr_flags);
int xfs_rename(struct xfs_inode *src_dp, struct xfs_name *src_name,
		struct xfs_inode *src_ip, struct xfs_inode *target_dp,
		struct xfs_name *target_name, struct xfs_inode *target_ip);
int xfs_attr_get(struct xfs_inode *ip, const unsigned char *name,
		unsigned char *value, int *valuelenp, int flags);
int xfs_attr_set(struct xfs_inode *dp, const unsigned char *name,
		unsigned char *value, int valuelen, int flags);
int xfs_attr_remove(struct xfs_inode *dp, const unsigned char *name, int flags);
int xfs_attr_list(struct xfs_inode *dp, char *buffer, int bufsize,
		int flags, struct attrlist_cursor_kern *cursor);
int xfs_bmap(struct xfs_inode *ip, xfs_off_t offset, ssize_t count,
		int flags, struct xfs_iomap *iomapp, int *niomaps);
void xfs_tosspages(struct xfs_inode *inode, xfs_off_t first,
		xfs_off_t last, int fiopt);
int xfs_flushinval_pages(struct xfs_inode *ip, xfs_off_t first,
		xfs_off_t last, int fiopt);
int xfs_flush_pages(struct xfs_inode *ip, xfs_off_t first,
		xfs_off_t last, uint64_t flags, int fiopt);
int xfs_wait_on_pages(struct xfs_inode *ip, xfs_off_t first, xfs_off_t last);

#endif /* _XFS_VNODEOPS_H */
