#include <linux/ncp_fs.h>
#include "ncp_fs_i.h"
#include "ncp_fs_sb.h"

/* define because it is easy to change PRINTK to {*}PRINTK */
#define PRINTK(format, args...) printk(KERN_DEBUG format , ## args)

#undef NCPFS_PARANOIA
#ifdef NCPFS_PARANOIA
#define PPRINTK(format, args...) PRINTK(format , ## args)
#else
#define PPRINTK(format, args...)
#endif

#ifndef DEBUG_NCP
#define DEBUG_NCP 0
#endif
#if DEBUG_NCP > 0
#define DPRINTK(format, args...) PRINTK(format , ## args)
#else
#define DPRINTK(format, args...)
#endif
#if DEBUG_NCP > 1
#define DDPRINTK(format, args...) PRINTK(format , ## args)
#else
#define DDPRINTK(format, args...)
#endif

#define NCP_MAX_RPC_TIMEOUT (6*HZ)


struct ncp_entry_info {
	struct nw_info_struct	i;
	ino_t			ino;
	int			opened;
	int			access;
	unsigned int		volume;
	__u8			file_handle[6];
};

static inline struct ncp_server *NCP_SBP(const struct super_block *sb)
{
	return sb->s_fs_info;
}

#define NCP_SERVER(inode)	NCP_SBP((inode)->i_sb)
static inline struct ncp_inode_info *NCP_FINFO(const struct inode *inode)
{
	return container_of(inode, struct ncp_inode_info, vfs_inode);
}

/* linux/fs/ncpfs/inode.c */
int ncp_notify_change(struct dentry *, struct iattr *);
struct inode *ncp_iget(struct super_block *, struct ncp_entry_info *);
void ncp_update_inode(struct inode *, struct ncp_entry_info *);
void ncp_update_inode2(struct inode *, struct ncp_entry_info *);

/* linux/fs/ncpfs/dir.c */
extern const struct inode_operations ncp_dir_inode_operations;
extern const struct file_operations ncp_dir_operations;
extern const struct dentry_operations ncp_dentry_operations;
int ncp_conn_logged_in(struct super_block *);
int ncp_date_dos2unix(__le16 time, __le16 date);
void ncp_date_unix2dos(int unix_date, __le16 * time, __le16 * date);

/* linux/fs/ncpfs/ioctl.c */
long ncp_ioctl(struct file *, unsigned int, unsigned long);
long ncp_compat_ioctl(struct file *, unsigned int, unsigned long);

/* linux/fs/ncpfs/sock.c */
int ncp_request2(struct ncp_server *server, int function,
	void* reply, int max_reply_size);
static inline int ncp_request(struct ncp_server *server, int function) {
	return ncp_request2(server, function, server->packet, server->packet_size);
}
int ncp_connect(struct ncp_server *server);
int ncp_disconnect(struct ncp_server *server);
void ncp_lock_server(struct ncp_server *server);
void ncp_unlock_server(struct ncp_server *server);

/* linux/fs/ncpfs/symlink.c */
#if defined(CONFIG_NCPFS_EXTRAS) || defined(CONFIG_NCPFS_NFS_NS)
extern const struct address_space_operations ncp_symlink_aops;
int ncp_symlink(struct inode*, struct dentry*, const char*);
#endif

/* linux/fs/ncpfs/file.c */
extern const struct inode_operations ncp_file_inode_operations;
extern const struct file_operations ncp_file_operations;
int ncp_make_open(struct inode *, int);

/* linux/fs/ncpfs/mmap.c */
int ncp_mmap(struct file *, struct vm_area_struct *);

/* linux/fs/ncpfs/ncplib_kernel.c */
int ncp_make_closed(struct inode *);

#include "ncplib_kernel.h"
