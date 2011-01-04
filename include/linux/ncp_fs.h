/*
 *  ncp_fs.h
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *
 */

#ifndef _LINUX_NCP_FS_H
#define _LINUX_NCP_FS_H

#include <linux/fs.h>
#include <linux/in.h>
#include <linux/types.h>
#include <linux/magic.h>

#include <linux/ipx.h>
#include <linux/ncp_no.h>

/*
 * ioctl commands
 */

struct ncp_ioctl_request {
	unsigned int function;
	unsigned int size;
	char __user *data;
};

struct ncp_fs_info {
	int version;
	struct sockaddr_ipx addr;
	__kernel_uid_t mounted_uid;
	int connection;		/* Connection number the server assigned us */
	int buffer_size;	/* The negotiated buffer size, to be
				   used for read/write requests! */

	int volume_number;
	__le32 directory_id;
};

struct ncp_fs_info_v2 {
	int version;
	unsigned long mounted_uid;
	unsigned int connection;
	unsigned int buffer_size;

	unsigned int volume_number;
	__le32 directory_id;

	__u32 dummy1;
	__u32 dummy2;
	__u32 dummy3;
};

struct ncp_sign_init
{
	char sign_root[8];
	char sign_last[16];
};

struct ncp_lock_ioctl
{
#define NCP_LOCK_LOG	0
#define NCP_LOCK_SH	1
#define NCP_LOCK_EX	2
#define NCP_LOCK_CLEAR	256
	int		cmd;
	int		origin;
	unsigned int	offset;
	unsigned int	length;
#define NCP_LOCK_DEFAULT_TIMEOUT	18
#define NCP_LOCK_MAX_TIMEOUT		180
	int		timeout;
};

struct ncp_setroot_ioctl
{
	int		volNumber;
	int		namespace;
	__le32		dirEntNum;
};

struct ncp_objectname_ioctl
{
#define NCP_AUTH_NONE	0x00
#define NCP_AUTH_BIND	0x31
#define NCP_AUTH_NDS	0x32
	int		auth_type;
	size_t		object_name_len;
	void __user *	object_name;	/* a userspace data, in most cases user name */
};

struct ncp_privatedata_ioctl
{
	size_t		len;
	void __user *	data;		/* ~1000 for NDS */
};

/* NLS charsets by ioctl */
#define NCP_IOCSNAME_LEN 20
struct ncp_nls_ioctl
{
	unsigned char codepage[NCP_IOCSNAME_LEN+1];
	unsigned char iocharset[NCP_IOCSNAME_LEN+1];
};

#define	NCP_IOC_NCPREQUEST		_IOR('n', 1, struct ncp_ioctl_request)
#define	NCP_IOC_GETMOUNTUID		_IOW('n', 2, __kernel_old_uid_t)
#define NCP_IOC_GETMOUNTUID2		_IOW('n', 2, unsigned long)

#define NCP_IOC_CONN_LOGGED_IN          _IO('n', 3)

#define NCP_GET_FS_INFO_VERSION    (1)
#define NCP_IOC_GET_FS_INFO             _IOWR('n', 4, struct ncp_fs_info)
#define NCP_GET_FS_INFO_VERSION_V2 (2)
#define NCP_IOC_GET_FS_INFO_V2		_IOWR('n', 4, struct ncp_fs_info_v2)

#define NCP_IOC_SIGN_INIT		_IOR('n', 5, struct ncp_sign_init)
#define NCP_IOC_SIGN_WANTED		_IOR('n', 6, int)
#define NCP_IOC_SET_SIGN_WANTED		_IOW('n', 6, int)

#define NCP_IOC_LOCKUNLOCK		_IOR('n', 7, struct ncp_lock_ioctl)

#define NCP_IOC_GETROOT			_IOW('n', 8, struct ncp_setroot_ioctl)
#define NCP_IOC_SETROOT			_IOR('n', 8, struct ncp_setroot_ioctl)

#define NCP_IOC_GETOBJECTNAME		_IOWR('n', 9, struct ncp_objectname_ioctl)
#define NCP_IOC_SETOBJECTNAME		_IOR('n', 9, struct ncp_objectname_ioctl)
#define NCP_IOC_GETPRIVATEDATA		_IOWR('n', 10, struct ncp_privatedata_ioctl)
#define NCP_IOC_SETPRIVATEDATA		_IOR('n', 10, struct ncp_privatedata_ioctl)

#define NCP_IOC_GETCHARSETS		_IOWR('n', 11, struct ncp_nls_ioctl)
#define NCP_IOC_SETCHARSETS		_IOR('n', 11, struct ncp_nls_ioctl)

#define NCP_IOC_GETDENTRYTTL		_IOW('n', 12, __u32)
#define NCP_IOC_SETDENTRYTTL		_IOR('n', 12, __u32)

/*
 * The packet size to allocate. One page should be enough.
 */
#define NCP_PACKET_SIZE 4070

#define NCP_MAXPATHLEN 255
#define NCP_MAXNAMELEN 14

#ifdef __KERNEL__

#include <linux/ncp_fs_i.h>
#include <linux/ncp_fs_sb.h>

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

static inline struct ncp_server *NCP_SBP(struct super_block *sb)
{
	return sb->s_fs_info;
}

#define NCP_SERVER(inode)	NCP_SBP((inode)->i_sb)
static inline struct ncp_inode_info *NCP_FINFO(struct inode *inode)
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
extern const struct dentry_operations ncp_root_dentry_operations;
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

#endif				/* __KERNEL__ */

#endif				/* _LINUX_NCP_FS_H */
