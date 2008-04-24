#ifndef _LINUX_NFS_XDR_H
#define _LINUX_NFS_XDR_H

#include <linux/nfsacl.h>

/*
 * To change the maximum rsize and wsize supported by the NFS client, adjust
 * NFS_MAX_FILE_IO_SIZE.  64KB is a typical maximum, but some servers can
 * support a megabyte or more.  The default is left at 4096 bytes, which is
 * reasonable for NFS over UDP.
 */
#define NFS_MAX_FILE_IO_SIZE	(1048576U)
#define NFS_DEF_FILE_IO_SIZE	(4096U)
#define NFS_MIN_FILE_IO_SIZE	(1024U)

struct nfs_fsid {
	uint64_t		major;
	uint64_t		minor;
};

/*
 * Helper for checking equality between 2 fsids.
 */
static inline int nfs_fsid_equal(const struct nfs_fsid *a, const struct nfs_fsid *b)
{
	return a->major == b->major && a->minor == b->minor;
}

struct nfs_fattr {
	unsigned short		valid;		/* which fields are valid */
	__u64			pre_size;	/* pre_op_attr.size	  */
	struct timespec		pre_mtime;	/* pre_op_attr.mtime	  */
	struct timespec		pre_ctime;	/* pre_op_attr.ctime	  */
	enum nfs_ftype		type;		/* always use NFSv2 types */
	__u32			mode;
	__u32			nlink;
	__u32			uid;
	__u32			gid;
	__u64			size;
	union {
		struct {
			__u32	blocksize;
			__u32	blocks;
		} nfs2;
		struct {
			__u64	used;
		} nfs3;
	} du;
	dev_t			rdev;
	struct nfs_fsid		fsid;
	__u64			fileid;
	struct timespec		atime;
	struct timespec		mtime;
	struct timespec		ctime;
	__u32			bitmap[2];	/* NFSv4 returned attribute bitmap */
	__u64			change_attr;	/* NFSv4 change attribute */
	__u64			pre_change_attr;/* pre-op NFSv4 change attribute */
	unsigned long		time_start;
};

#define NFS_ATTR_WCC		0x0001		/* pre-op WCC data    */
#define NFS_ATTR_FATTR		0x0002		/* post-op attributes */
#define NFS_ATTR_FATTR_V3	0x0004		/* NFSv3 attributes */
#define NFS_ATTR_FATTR_V4	0x0008		/* NFSv4 change attribute */
#define NFS_ATTR_WCC_V4		0x0010		/* pre-op change attribute */
#define NFS_ATTR_FATTR_V4_REFERRAL	0x0020		/* NFSv4 referral */

/*
 * Info on the file system
 */
struct nfs_fsinfo {
	struct nfs_fattr	*fattr; /* Post-op attributes */
	__u32			rtmax;	/* max.  read transfer size */
	__u32			rtpref;	/* pref. read transfer size */
	__u32			rtmult;	/* reads should be multiple of this */
	__u32			wtmax;	/* max.  write transfer size */
	__u32			wtpref;	/* pref. write transfer size */
	__u32			wtmult;	/* writes should be multiple of this */
	__u32			dtpref;	/* pref. readdir transfer size */
	__u64			maxfilesize;
	__u32			lease_time; /* in seconds */
};

struct nfs_fsstat {
	struct nfs_fattr	*fattr; /* Post-op attributes */
	__u64			tbytes;	/* total size in bytes */
	__u64			fbytes;	/* # of free bytes */
	__u64			abytes;	/* # of bytes available to user */
	__u64			tfiles;	/* # of files */
	__u64			ffiles;	/* # of free files */
	__u64			afiles;	/* # of files available to user */
};

struct nfs2_fsstat {
	__u32			tsize;  /* Server transfer size */
	__u32			bsize;  /* Filesystem block size */
	__u32			blocks; /* No. of "bsize" blocks on filesystem */
	__u32			bfree;  /* No. of free "bsize" blocks */
	__u32			bavail; /* No. of available "bsize" blocks */
};

struct nfs_pathconf {
	struct nfs_fattr	*fattr; /* Post-op attributes */
	__u32			max_link; /* max # of hard links */
	__u32			max_namelen; /* max name length */
};

struct nfs4_change_info {
	u32			atomic;
	u64			before;
	u64			after;
};

struct nfs_seqid;
/*
 * Arguments to the open call.
 */
struct nfs_openargs {
	const struct nfs_fh *	fh;
	struct nfs_seqid *	seqid;
	int			open_flags;
	__u64                   clientid;
	__u64                   id;
	union {
		struct iattr *  attrs;    /* UNCHECKED, GUARDED */
		nfs4_verifier   verifier; /* EXCLUSIVE */
		nfs4_stateid	delegation;		/* CLAIM_DELEGATE_CUR */
		int		delegation_type;	/* CLAIM_PREVIOUS */
	} u;
	const struct qstr *	name;
	const struct nfs_server *server;	 /* Needed for ID mapping */
	const u32 *		bitmask;
	__u32			claim;
};

struct nfs_openres {
	nfs4_stateid            stateid;
	struct nfs_fh           fh;
	struct nfs4_change_info	cinfo;
	__u32                   rflags;
	struct nfs_fattr *      f_attr;
	struct nfs_fattr *      dir_attr;
	struct nfs_seqid *	seqid;
	const struct nfs_server *server;
	int			delegation_type;
	nfs4_stateid		delegation;
	__u32			do_recall;
	__u64			maxsize;
	__u32			attrset[NFS4_BITMAP_SIZE];
};

/*
 * Arguments to the open_confirm call.
 */
struct nfs_open_confirmargs {
	const struct nfs_fh *	fh;
	nfs4_stateid *		stateid;
	struct nfs_seqid *	seqid;
};

struct nfs_open_confirmres {
	nfs4_stateid            stateid;
	struct nfs_seqid *	seqid;
};

/*
 * Arguments to the close call.
 */
struct nfs_closeargs {
	struct nfs_fh *         fh;
	nfs4_stateid *		stateid;
	struct nfs_seqid *	seqid;
	int			open_flags;
	const u32 *		bitmask;
};

struct nfs_closeres {
	nfs4_stateid            stateid;
	struct nfs_fattr *	fattr;
	struct nfs_seqid *	seqid;
	const struct nfs_server *server;
};
/*
 *  * Arguments to the lock,lockt, and locku call.
 *   */
struct nfs_lowner {
	__u64			clientid;
	__u64			id;
};

struct nfs_lock_args {
	struct nfs_fh *		fh;
	struct file_lock *	fl;
	struct nfs_seqid *	lock_seqid;
	nfs4_stateid *		lock_stateid;
	struct nfs_seqid *	open_seqid;
	nfs4_stateid *		open_stateid;
	struct nfs_lowner	lock_owner;
	unsigned char		block : 1;
	unsigned char		reclaim : 1;
	unsigned char		new_lock_owner : 1;
};

struct nfs_lock_res {
	nfs4_stateid		stateid;
	struct nfs_seqid *	lock_seqid;
	struct nfs_seqid *	open_seqid;
};

struct nfs_locku_args {
	struct nfs_fh *		fh;
	struct file_lock *	fl;
	struct nfs_seqid *	seqid;
	nfs4_stateid *		stateid;
};

struct nfs_locku_res {
	nfs4_stateid		stateid;
	struct nfs_seqid *	seqid;
};

struct nfs_lockt_args {
	struct nfs_fh *		fh;
	struct file_lock *	fl;
	struct nfs_lowner	lock_owner;
};

struct nfs_lockt_res {
	struct file_lock *	denied; /* LOCK, LOCKT failed */
};

struct nfs4_delegreturnargs {
	const struct nfs_fh *fhandle;
	const nfs4_stateid *stateid;
	const u32 * bitmask;
};

struct nfs4_delegreturnres {
	struct nfs_fattr * fattr;
	const struct nfs_server *server;
};

/*
 * Arguments to the read call.
 */
struct nfs_readargs {
	struct nfs_fh *		fh;
	struct nfs_open_context *context;
	__u64			offset;
	__u32			count;
	unsigned int		pgbase;
	struct page **		pages;
};

struct nfs_readres {
	struct nfs_fattr *	fattr;
	__u32			count;
	int                     eof;
};

/*
 * Arguments to the write call.
 */
struct nfs_writeargs {
	struct nfs_fh *		fh;
	struct nfs_open_context *context;
	__u64			offset;
	__u32			count;
	enum nfs3_stable_how	stable;
	unsigned int		pgbase;
	struct page **		pages;
	const u32 *		bitmask;
};

struct nfs_writeverf {
	enum nfs3_stable_how	committed;
	__be32			verifier[2];
};

struct nfs_writeres {
	struct nfs_fattr *	fattr;
	struct nfs_writeverf *	verf;
	__u32			count;
	const struct nfs_server *server;
};

/*
 * Common arguments to the unlink call
 */
struct nfs_removeargs {
	const struct nfs_fh	*fh;
	struct qstr		name;
	const u32 *		bitmask;
};

struct nfs_removeres {
	const struct nfs_server *server;
	struct nfs4_change_info	cinfo;
	struct nfs_fattr	dir_attr;
};

/*
 * Argument struct for decode_entry function
 */
struct nfs_entry {
	__u64			ino;
	__u64			cookie,
				prev_cookie;
	const char *		name;
	unsigned int		len;
	int			eof;
	struct nfs_fh *		fh;
	struct nfs_fattr *	fattr;
};

/*
 * The following types are for NFSv2 only.
 */
struct nfs_sattrargs {
	struct nfs_fh *		fh;
	struct iattr *		sattr;
};

struct nfs_diropargs {
	struct nfs_fh *		fh;
	const char *		name;
	unsigned int		len;
};

struct nfs_createargs {
	struct nfs_fh *		fh;
	const char *		name;
	unsigned int		len;
	struct iattr *		sattr;
};

struct nfs_renameargs {
	struct nfs_fh *		fromfh;
	const char *		fromname;
	unsigned int		fromlen;
	struct nfs_fh *		tofh;
	const char *		toname;
	unsigned int		tolen;
};

struct nfs_setattrargs {
	struct nfs_fh *                 fh;
	nfs4_stateid                    stateid;
	struct iattr *                  iap;
	const struct nfs_server *	server; /* Needed for name mapping */
	const u32 *			bitmask;
};

struct nfs_setaclargs {
	struct nfs_fh *			fh;
	size_t				acl_len;
	unsigned int			acl_pgbase;
	struct page **			acl_pages;
};

struct nfs_getaclargs {
	struct nfs_fh *			fh;
	size_t				acl_len;
	unsigned int			acl_pgbase;
	struct page **			acl_pages;
};

struct nfs_setattrres {
	struct nfs_fattr *              fattr;
	const struct nfs_server *	server;
};

struct nfs_linkargs {
	struct nfs_fh *		fromfh;
	struct nfs_fh *		tofh;
	const char *		toname;
	unsigned int		tolen;
};

struct nfs_symlinkargs {
	struct nfs_fh *		fromfh;
	const char *		fromname;
	unsigned int		fromlen;
	struct page **		pages;
	unsigned int		pathlen;
	struct iattr *		sattr;
};

struct nfs_readdirargs {
	struct nfs_fh *		fh;
	__u32			cookie;
	unsigned int		count;
	struct page **		pages;
};

struct nfs3_getaclargs {
	struct nfs_fh *		fh;
	int			mask;
	struct page **		pages;
};

struct nfs3_setaclargs {
	struct inode *		inode;
	int			mask;
	struct posix_acl *	acl_access;
	struct posix_acl *	acl_default;
	struct page **		pages;
};

struct nfs_diropok {
	struct nfs_fh *		fh;
	struct nfs_fattr *	fattr;
};

struct nfs_readlinkargs {
	struct nfs_fh *		fh;
	unsigned int		pgbase;
	unsigned int		pglen;
	struct page **		pages;
};

struct nfs3_sattrargs {
	struct nfs_fh *		fh;
	struct iattr *		sattr;
	unsigned int		guard;
	struct timespec		guardtime;
};

struct nfs3_diropargs {
	struct nfs_fh *		fh;
	const char *		name;
	unsigned int		len;
};

struct nfs3_accessargs {
	struct nfs_fh *		fh;
	__u32			access;
};

struct nfs3_createargs {
	struct nfs_fh *		fh;
	const char *		name;
	unsigned int		len;
	struct iattr *		sattr;
	enum nfs3_createmode	createmode;
	__be32			verifier[2];
};

struct nfs3_mkdirargs {
	struct nfs_fh *		fh;
	const char *		name;
	unsigned int		len;
	struct iattr *		sattr;
};

struct nfs3_symlinkargs {
	struct nfs_fh *		fromfh;
	const char *		fromname;
	unsigned int		fromlen;
	struct page **		pages;
	unsigned int		pathlen;
	struct iattr *		sattr;
};

struct nfs3_mknodargs {
	struct nfs_fh *		fh;
	const char *		name;
	unsigned int		len;
	enum nfs3_ftype		type;
	struct iattr *		sattr;
	dev_t			rdev;
};

struct nfs3_renameargs {
	struct nfs_fh *		fromfh;
	const char *		fromname;
	unsigned int		fromlen;
	struct nfs_fh *		tofh;
	const char *		toname;
	unsigned int		tolen;
};

struct nfs3_linkargs {
	struct nfs_fh *		fromfh;
	struct nfs_fh *		tofh;
	const char *		toname;
	unsigned int		tolen;
};

struct nfs3_readdirargs {
	struct nfs_fh *		fh;
	__u64			cookie;
	__be32			verf[2];
	int			plus;
	unsigned int            count;
	struct page **		pages;
};

struct nfs3_diropres {
	struct nfs_fattr *	dir_attr;
	struct nfs_fh *		fh;
	struct nfs_fattr *	fattr;
};

struct nfs3_accessres {
	struct nfs_fattr *	fattr;
	__u32			access;
};

struct nfs3_readlinkargs {
	struct nfs_fh *		fh;
	unsigned int		pgbase;
	unsigned int		pglen;
	struct page **		pages;
};

struct nfs3_renameres {
	struct nfs_fattr *	fromattr;
	struct nfs_fattr *	toattr;
};

struct nfs3_linkres {
	struct nfs_fattr *	dir_attr;
	struct nfs_fattr *	fattr;
};

struct nfs3_readdirres {
	struct nfs_fattr *	dir_attr;
	__be32 *		verf;
	int			plus;
};

struct nfs3_getaclres {
	struct nfs_fattr *	fattr;
	int			mask;
	unsigned int		acl_access_count;
	unsigned int		acl_default_count;
	struct posix_acl *	acl_access;
	struct posix_acl *	acl_default;
};

#ifdef CONFIG_NFS_V4

typedef u64 clientid4;

struct nfs4_accessargs {
	const struct nfs_fh *		fh;
	const u32 *			bitmask;
	u32				access;
};

struct nfs4_accessres {
	const struct nfs_server *	server;
	struct nfs_fattr *		fattr;
	u32				supported;
	u32				access;
};

struct nfs4_create_arg {
	u32				ftype;
	union {
		struct {
			struct page **	pages;
			unsigned int	len;
		} symlink;   /* NF4LNK */
		struct {
			u32		specdata1;
			u32		specdata2;
		} device;    /* NF4BLK, NF4CHR */
	} u;
	const struct qstr *		name;
	const struct nfs_server *	server;
	const struct iattr *		attrs;
	const struct nfs_fh *		dir_fh;
	const u32 *			bitmask;
};

struct nfs4_create_res {
	const struct nfs_server *	server;
	struct nfs_fh *			fh;
	struct nfs_fattr *		fattr;
	struct nfs4_change_info		dir_cinfo;
	struct nfs_fattr *		dir_fattr;
};

struct nfs4_fsinfo_arg {
	const struct nfs_fh *		fh;
	const u32 *			bitmask;
};

struct nfs4_getattr_arg {
	const struct nfs_fh *		fh;
	const u32 *			bitmask;
};

struct nfs4_getattr_res {
	const struct nfs_server *	server;
	struct nfs_fattr *		fattr;
};

struct nfs4_link_arg {
	const struct nfs_fh *		fh;
	const struct nfs_fh *		dir_fh;
	const struct qstr *		name;
	const u32 *			bitmask;
};

struct nfs4_link_res {
	const struct nfs_server *	server;
	struct nfs_fattr *		fattr;
	struct nfs4_change_info		cinfo;
	struct nfs_fattr *		dir_attr;
};


struct nfs4_lookup_arg {
	const struct nfs_fh *		dir_fh;
	const struct qstr *		name;
	const u32 *			bitmask;
};

struct nfs4_lookup_res {
	const struct nfs_server *	server;
	struct nfs_fattr *		fattr;
	struct nfs_fh *			fh;
};

struct nfs4_lookup_root_arg {
	const u32 *			bitmask;
};

struct nfs4_pathconf_arg {
	const struct nfs_fh *		fh;
	const u32 *			bitmask;
};

struct nfs4_readdir_arg {
	const struct nfs_fh *		fh;
	u64				cookie;
	nfs4_verifier			verifier;
	u32				count;
	struct page **			pages;	/* zero-copy data */
	unsigned int			pgbase;	/* zero-copy data */
	const u32 *			bitmask;
};

struct nfs4_readdir_res {
	nfs4_verifier			verifier;
	unsigned int			pgbase;
};

struct nfs4_readlink {
	const struct nfs_fh *		fh;
	unsigned int			pgbase;
	unsigned int			pglen;   /* zero-copy data */
	struct page **			pages;   /* zero-copy data */
};

struct nfs4_rename_arg {
	const struct nfs_fh *		old_dir;
	const struct nfs_fh *		new_dir;
	const struct qstr *		old_name;
	const struct qstr *		new_name;
	const u32 *			bitmask;
};

struct nfs4_rename_res {
	const struct nfs_server *	server;
	struct nfs4_change_info		old_cinfo;
	struct nfs_fattr *		old_fattr;
	struct nfs4_change_info		new_cinfo;
	struct nfs_fattr *		new_fattr;
};

#define NFS4_SETCLIENTID_NAMELEN	(56)
struct nfs4_setclientid {
	const nfs4_verifier *		sc_verifier;
	unsigned int			sc_name_len;
	char				sc_name[NFS4_SETCLIENTID_NAMELEN];
	u32				sc_prog;
	unsigned int			sc_netid_len;
	char				sc_netid[RPCBIND_MAXNETIDLEN];
	unsigned int			sc_uaddr_len;
	char				sc_uaddr[RPCBIND_MAXUADDRLEN];
	u32				sc_cb_ident;
};

struct nfs4_statfs_arg {
	const struct nfs_fh *		fh;
	const u32 *			bitmask;
};

struct nfs4_server_caps_res {
	u32				attr_bitmask[2];
	u32				acl_bitmask;
	u32				has_links;
	u32				has_symlinks;
};

struct nfs4_string {
	unsigned int len;
	char *data;
};

#define NFS4_PATHNAME_MAXCOMPONENTS 512
struct nfs4_pathname {
	unsigned int ncomponents;
	struct nfs4_string components[NFS4_PATHNAME_MAXCOMPONENTS];
};

#define NFS4_FS_LOCATION_MAXSERVERS 10
struct nfs4_fs_location {
	unsigned int nservers;
	struct nfs4_string servers[NFS4_FS_LOCATION_MAXSERVERS];
	struct nfs4_pathname rootpath;
};

#define NFS4_FS_LOCATIONS_MAXENTRIES 10
struct nfs4_fs_locations {
	struct nfs_fattr fattr;
	const struct nfs_server *server;
	struct nfs4_pathname fs_path;
	int nlocations;
	struct nfs4_fs_location locations[NFS4_FS_LOCATIONS_MAXENTRIES];
};

struct nfs4_fs_locations_arg {
	const struct nfs_fh *dir_fh;
	const struct qstr *name;
	struct page *page;
	const u32 *bitmask;
};

#endif /* CONFIG_NFS_V4 */

struct nfs_page;

#define NFS_PAGEVEC_SIZE	(8U)

struct nfs_read_data {
	int			flags;
	struct rpc_task		task;
	struct inode		*inode;
	struct rpc_cred		*cred;
	struct nfs_fattr	fattr;	/* fattr storage */
	struct list_head	pages;	/* Coalesced read requests */
	struct nfs_page		*req;	/* multi ops per nfs_page */
	struct page		**pagevec;
	unsigned int		npages;	/* Max length of pagevec */
	struct nfs_readargs args;
	struct nfs_readres  res;
#ifdef CONFIG_NFS_V4
	unsigned long		timestamp;	/* For lease renewal */
#endif
	struct page		*page_array[NFS_PAGEVEC_SIZE];
};

struct nfs_write_data {
	int			flags;
	struct rpc_task		task;
	struct inode		*inode;
	struct rpc_cred		*cred;
	struct nfs_fattr	fattr;
	struct nfs_writeverf	verf;
	struct list_head	pages;		/* Coalesced requests we wish to flush */
	struct nfs_page		*req;		/* multi ops per nfs_page */
	struct page		**pagevec;
	unsigned int		npages;		/* Max length of pagevec */
	struct nfs_writeargs	args;		/* argument struct */
	struct nfs_writeres	res;		/* result struct */
#ifdef CONFIG_NFS_V4
	unsigned long		timestamp;	/* For lease renewal */
#endif
	struct page		*page_array[NFS_PAGEVEC_SIZE];
};

struct nfs_access_entry;

/*
 * RPC procedure vector for NFSv2/NFSv3 demuxing
 */
struct nfs_rpc_ops {
	u32	version;		/* Protocol version */
	struct dentry_operations *dentry_ops;
	const struct inode_operations *dir_inode_ops;
	const struct inode_operations *file_inode_ops;

	int	(*getroot) (struct nfs_server *, struct nfs_fh *,
			    struct nfs_fsinfo *);
	int	(*lookupfh)(struct nfs_server *, struct nfs_fh *,
			    struct qstr *, struct nfs_fh *,
			    struct nfs_fattr *);
	int	(*getattr) (struct nfs_server *, struct nfs_fh *,
			    struct nfs_fattr *);
	int	(*setattr) (struct dentry *, struct nfs_fattr *,
			    struct iattr *);
	int	(*lookup)  (struct inode *, struct qstr *,
			    struct nfs_fh *, struct nfs_fattr *);
	int	(*access)  (struct inode *, struct nfs_access_entry *);
	int	(*readlink)(struct inode *, struct page *, unsigned int,
			    unsigned int);
	int	(*create)  (struct inode *, struct dentry *,
			    struct iattr *, int, struct nameidata *);
	int	(*remove)  (struct inode *, struct qstr *);
	void	(*unlink_setup)  (struct rpc_message *, struct inode *dir);
	int	(*unlink_done) (struct rpc_task *, struct inode *);
	int	(*rename)  (struct inode *, struct qstr *,
			    struct inode *, struct qstr *);
	int	(*link)    (struct inode *, struct inode *, struct qstr *);
	int	(*symlink) (struct inode *, struct dentry *, struct page *,
			    unsigned int, struct iattr *);
	int	(*mkdir)   (struct inode *, struct dentry *, struct iattr *);
	int	(*rmdir)   (struct inode *, struct qstr *);
	int	(*readdir) (struct dentry *, struct rpc_cred *,
			    u64, struct page *, unsigned int, int);
	int	(*mknod)   (struct inode *, struct dentry *, struct iattr *,
			    dev_t);
	int	(*statfs)  (struct nfs_server *, struct nfs_fh *,
			    struct nfs_fsstat *);
	int	(*fsinfo)  (struct nfs_server *, struct nfs_fh *,
			    struct nfs_fsinfo *);
	int	(*pathconf) (struct nfs_server *, struct nfs_fh *,
			     struct nfs_pathconf *);
	int	(*set_capabilities)(struct nfs_server *, struct nfs_fh *);
	__be32 *(*decode_dirent)(__be32 *, struct nfs_entry *, int plus);
	void	(*read_setup)   (struct nfs_read_data *, struct rpc_message *);
	int	(*read_done)  (struct rpc_task *, struct nfs_read_data *);
	void	(*write_setup)  (struct nfs_write_data *, struct rpc_message *);
	int	(*write_done)  (struct rpc_task *, struct nfs_write_data *);
	void	(*commit_setup) (struct nfs_write_data *, struct rpc_message *);
	int	(*commit_done) (struct rpc_task *, struct nfs_write_data *);
	int	(*file_open)   (struct inode *, struct file *);
	int	(*file_release) (struct inode *, struct file *);
	int	(*lock)(struct file *, int, struct file_lock *);
	void	(*clear_acl_cache)(struct inode *);
};

/*
 * 	NFS_CALL(getattr, inode, (fattr));
 * into
 *	NFS_PROTO(inode)->getattr(fattr);
 */
#define NFS_CALL(op, inode, args)	NFS_PROTO(inode)->op args

/*
 * Function vectors etc. for the NFS client
 */
extern const struct nfs_rpc_ops	nfs_v2_clientops;
extern const struct nfs_rpc_ops	nfs_v3_clientops;
extern const struct nfs_rpc_ops	nfs_v4_clientops;
extern struct rpc_version	nfs_version2;
extern struct rpc_version	nfs_version3;
extern struct rpc_version	nfs_version4;

extern struct rpc_version	nfsacl_version3;
extern struct rpc_program	nfsacl_program;

#endif
