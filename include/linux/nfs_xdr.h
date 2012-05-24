#ifndef _LINUX_NFS_XDR_H
#define _LINUX_NFS_XDR_H

#include <linux/nfsacl.h>
#include <linux/sunrpc/gss_api.h>

/*
 * To change the maximum rsize and wsize supported by the NFS client, adjust
 * NFS_MAX_FILE_IO_SIZE.  64KB is a typical maximum, but some servers can
 * support a megabyte or more.  The default is left at 4096 bytes, which is
 * reasonable for NFS over UDP.
 */
#define NFS_MAX_FILE_IO_SIZE	(1048576U)
#define NFS_DEF_FILE_IO_SIZE	(4096U)
#define NFS_MIN_FILE_IO_SIZE	(1024U)

/* Forward declaration for NFS v3 */
struct nfs4_secinfo_flavors;

struct nfs4_string {
	unsigned int len;
	char *data;
};

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
	unsigned int		valid;		/* which fields are valid */
	umode_t			mode;
	__u32			nlink;
	__u32			uid;
	__u32			gid;
	dev_t			rdev;
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
	struct nfs_fsid		fsid;
	__u64			fileid;
	__u64			mounted_on_fileid;
	struct timespec		atime;
	struct timespec		mtime;
	struct timespec		ctime;
	__u64			change_attr;	/* NFSv4 change attribute */
	__u64			pre_change_attr;/* pre-op NFSv4 change attribute */
	__u64			pre_size;	/* pre_op_attr.size	  */
	struct timespec		pre_mtime;	/* pre_op_attr.mtime	  */
	struct timespec		pre_ctime;	/* pre_op_attr.ctime	  */
	unsigned long		time_start;
	unsigned long		gencount;
	struct nfs4_string	*owner_name;
	struct nfs4_string	*group_name;
};

#define NFS_ATTR_FATTR_TYPE		(1U << 0)
#define NFS_ATTR_FATTR_MODE		(1U << 1)
#define NFS_ATTR_FATTR_NLINK		(1U << 2)
#define NFS_ATTR_FATTR_OWNER		(1U << 3)
#define NFS_ATTR_FATTR_GROUP		(1U << 4)
#define NFS_ATTR_FATTR_RDEV		(1U << 5)
#define NFS_ATTR_FATTR_SIZE		(1U << 6)
#define NFS_ATTR_FATTR_PRESIZE		(1U << 7)
#define NFS_ATTR_FATTR_BLOCKS_USED	(1U << 8)
#define NFS_ATTR_FATTR_SPACE_USED	(1U << 9)
#define NFS_ATTR_FATTR_FSID		(1U << 10)
#define NFS_ATTR_FATTR_FILEID		(1U << 11)
#define NFS_ATTR_FATTR_ATIME		(1U << 12)
#define NFS_ATTR_FATTR_MTIME		(1U << 13)
#define NFS_ATTR_FATTR_CTIME		(1U << 14)
#define NFS_ATTR_FATTR_PREMTIME		(1U << 15)
#define NFS_ATTR_FATTR_PRECTIME		(1U << 16)
#define NFS_ATTR_FATTR_CHANGE		(1U << 17)
#define NFS_ATTR_FATTR_PRECHANGE	(1U << 18)
#define NFS_ATTR_FATTR_V4_LOCATIONS	(1U << 19)
#define NFS_ATTR_FATTR_V4_REFERRAL	(1U << 20)
#define NFS_ATTR_FATTR_MOUNTPOINT	(1U << 21)
#define NFS_ATTR_FATTR_MOUNTED_ON_FILEID (1U << 22)
#define NFS_ATTR_FATTR_OWNER_NAME	(1U << 23)
#define NFS_ATTR_FATTR_GROUP_NAME	(1U << 24)

#define NFS_ATTR_FATTR (NFS_ATTR_FATTR_TYPE \
		| NFS_ATTR_FATTR_MODE \
		| NFS_ATTR_FATTR_NLINK \
		| NFS_ATTR_FATTR_OWNER \
		| NFS_ATTR_FATTR_GROUP \
		| NFS_ATTR_FATTR_RDEV \
		| NFS_ATTR_FATTR_SIZE \
		| NFS_ATTR_FATTR_FSID \
		| NFS_ATTR_FATTR_FILEID \
		| NFS_ATTR_FATTR_ATIME \
		| NFS_ATTR_FATTR_MTIME \
		| NFS_ATTR_FATTR_CTIME)
#define NFS_ATTR_FATTR_V2 (NFS_ATTR_FATTR \
		| NFS_ATTR_FATTR_BLOCKS_USED)
#define NFS_ATTR_FATTR_V3 (NFS_ATTR_FATTR \
		| NFS_ATTR_FATTR_SPACE_USED)
#define NFS_ATTR_FATTR_V4 (NFS_ATTR_FATTR \
		| NFS_ATTR_FATTR_SPACE_USED \
		| NFS_ATTR_FATTR_CHANGE)

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
	struct timespec		time_delta; /* server time granularity */
	__u32			lease_time; /* in seconds */
	__u32			layouttype; /* supported pnfs layout driver */
	__u32			blksize; /* preferred pnfs io block size */
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

/* nfs41 sessions channel attributes */
struct nfs4_channel_attrs {
	u32			max_rqst_sz;
	u32			max_resp_sz;
	u32			max_resp_sz_cached;
	u32			max_ops;
	u32			max_reqs;
};

/* nfs41 sessions slot seqid */
struct nfs4_slot {
	u32		 	seq_nr;
};

struct nfs4_sequence_args {
	struct nfs4_session	*sa_session;
	u32			sa_slotid;
	u8			sa_cache_this;
};

struct nfs4_sequence_res {
	struct nfs4_session	*sr_session;
	struct nfs4_slot	*sr_slot;	/* slot used to send request */
	int			sr_status;	/* sequence operation status */
	unsigned long		sr_renewal_time;
	u32			sr_status_flags;
};

struct nfs4_get_lease_time_args {
	struct nfs4_sequence_args	la_seq_args;
};

struct nfs4_get_lease_time_res {
	struct nfs_fsinfo	       *lr_fsinfo;
	struct nfs4_sequence_res	lr_seq_res;
};

#define PNFS_LAYOUT_MAXSIZE 4096

struct nfs4_layoutdriver_data {
	struct page **pages;
	__u32 pglen;
	__u32 len;
};

struct pnfs_layout_range {
	u32 iomode;
	u64 offset;
	u64 length;
};

struct nfs4_layoutget_args {
	__u32 type;
	struct pnfs_layout_range range;
	__u64 minlength;
	__u32 maxcount;
	struct inode *inode;
	struct nfs_open_context *ctx;
	struct nfs4_sequence_args seq_args;
	nfs4_stateid stateid;
	struct nfs4_layoutdriver_data layout;
};

struct nfs4_layoutget_res {
	__u32 return_on_close;
	struct pnfs_layout_range range;
	__u32 type;
	nfs4_stateid stateid;
	struct nfs4_sequence_res seq_res;
	struct nfs4_layoutdriver_data *layoutp;
};

struct nfs4_layoutget {
	struct nfs4_layoutget_args args;
	struct nfs4_layoutget_res res;
	struct pnfs_layout_segment **lsegpp;
	gfp_t gfp_flags;
};

struct nfs4_getdevicelist_args {
	const struct nfs_fh *fh;
	u32 layoutclass;
	struct nfs4_sequence_args seq_args;
};

struct nfs4_getdevicelist_res {
	struct pnfs_devicelist *devlist;
	struct nfs4_sequence_res seq_res;
};

struct nfs4_getdeviceinfo_args {
	struct pnfs_device *pdev;
	struct nfs4_sequence_args seq_args;
};

struct nfs4_getdeviceinfo_res {
	struct pnfs_device *pdev;
	struct nfs4_sequence_res seq_res;
};

struct nfs4_layoutcommit_args {
	nfs4_stateid stateid;
	__u64 lastbytewritten;
	struct inode *inode;
	const u32 *bitmask;
	struct nfs4_sequence_args seq_args;
};

struct nfs4_layoutcommit_res {
	struct nfs_fattr *fattr;
	const struct nfs_server *server;
	struct nfs4_sequence_res seq_res;
	int status;
};

struct nfs4_layoutcommit_data {
	struct rpc_task task;
	struct nfs_fattr fattr;
	struct list_head lseg_list;
	struct rpc_cred *cred;
	struct nfs4_layoutcommit_args args;
	struct nfs4_layoutcommit_res res;
};

struct nfs4_layoutreturn_args {
	struct pnfs_layout_hdr *layout;
	struct inode *inode;
	nfs4_stateid stateid;
	__u32   layout_type;
	struct nfs4_sequence_args seq_args;
};

struct nfs4_layoutreturn_res {
	struct nfs4_sequence_res seq_res;
	u32 lrs_present;
	nfs4_stateid stateid;
};

struct nfs4_layoutreturn {
	struct nfs4_layoutreturn_args args;
	struct nfs4_layoutreturn_res res;
	struct rpc_cred *cred;
	struct nfs_client *clp;
	int rpc_status;
};

/*
 * Arguments to the open call.
 */
struct nfs_openargs {
	const struct nfs_fh *	fh;
	struct nfs_seqid *	seqid;
	int			open_flags;
	fmode_t			fmode;
	__u64                   clientid;
	__u64                   id;
	union {
		struct {
			struct iattr *  attrs;    /* UNCHECKED, GUARDED */
			nfs4_verifier   verifier; /* EXCLUSIVE */
		};
		nfs4_stateid	delegation;		/* CLAIM_DELEGATE_CUR */
		fmode_t		delegation_type;	/* CLAIM_PREVIOUS */
	} u;
	const struct qstr *	name;
	const struct nfs_server *server;	 /* Needed for ID mapping */
	const u32 *		bitmask;
	const u32 *		dir_bitmask;
	__u32			claim;
	struct nfs4_sequence_args	seq_args;
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
	fmode_t			delegation_type;
	nfs4_stateid		delegation;
	__u32			do_recall;
	__u64			maxsize;
	__u32			attrset[NFS4_BITMAP_SIZE];
	struct nfs4_string	*owner;
	struct nfs4_string	*group_owner;
	struct nfs4_sequence_res	seq_res;
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
	fmode_t			fmode;
	const u32 *		bitmask;
	struct nfs4_sequence_args	seq_args;
};

struct nfs_closeres {
	nfs4_stateid            stateid;
	struct nfs_fattr *	fattr;
	struct nfs_seqid *	seqid;
	const struct nfs_server *server;
	struct nfs4_sequence_res	seq_res;
};
/*
 *  * Arguments to the lock,lockt, and locku call.
 *   */
struct nfs_lowner {
	__u64			clientid;
	__u64			id;
	dev_t			s_dev;
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
	struct nfs4_sequence_args	seq_args;
};

struct nfs_lock_res {
	nfs4_stateid		stateid;
	struct nfs_seqid *	lock_seqid;
	struct nfs_seqid *	open_seqid;
	struct nfs4_sequence_res	seq_res;
};

struct nfs_locku_args {
	struct nfs_fh *		fh;
	struct file_lock *	fl;
	struct nfs_seqid *	seqid;
	nfs4_stateid *		stateid;
	struct nfs4_sequence_args	seq_args;
};

struct nfs_locku_res {
	nfs4_stateid		stateid;
	struct nfs_seqid *	seqid;
	struct nfs4_sequence_res	seq_res;
};

struct nfs_lockt_args {
	struct nfs_fh *		fh;
	struct file_lock *	fl;
	struct nfs_lowner	lock_owner;
	struct nfs4_sequence_args	seq_args;
};

struct nfs_lockt_res {
	struct file_lock *	denied; /* LOCK, LOCKT failed */
	struct nfs4_sequence_res	seq_res;
};

struct nfs_release_lockowner_args {
	struct nfs_lowner	lock_owner;
};

struct nfs4_delegreturnargs {
	const struct nfs_fh *fhandle;
	const nfs4_stateid *stateid;
	const u32 * bitmask;
	struct nfs4_sequence_args	seq_args;
};

struct nfs4_delegreturnres {
	struct nfs_fattr * fattr;
	const struct nfs_server *server;
	struct nfs4_sequence_res	seq_res;
};

/*
 * Arguments to the read call.
 */
struct nfs_readargs {
	struct nfs_fh *		fh;
	struct nfs_open_context *context;
	struct nfs_lock_context *lock_context;
	__u64			offset;
	__u32			count;
	unsigned int		pgbase;
	struct page **		pages;
	struct nfs4_sequence_args	seq_args;
};

struct nfs_readres {
	struct nfs_fattr *	fattr;
	__u32			count;
	int                     eof;
	struct nfs4_sequence_res	seq_res;
};

/*
 * Arguments to the write call.
 */
struct nfs_writeargs {
	struct nfs_fh *		fh;
	struct nfs_open_context *context;
	struct nfs_lock_context *lock_context;
	__u64			offset;
	__u32			count;
	enum nfs3_stable_how	stable;
	unsigned int		pgbase;
	struct page **		pages;
	const u32 *		bitmask;
	struct nfs4_sequence_args	seq_args;
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
	struct nfs4_sequence_res	seq_res;
};

/*
 * Common arguments to the unlink call
 */
struct nfs_removeargs {
	const struct nfs_fh	*fh;
	struct qstr		name;
	const u32 *		bitmask;
	struct nfs4_sequence_args	seq_args;
};

struct nfs_removeres {
	const struct nfs_server *server;
	struct nfs_fattr	*dir_attr;
	struct nfs4_change_info	cinfo;
	struct nfs4_sequence_res 	seq_res;
};

/*
 * Common arguments to the rename call
 */
struct nfs_renameargs {
	const struct nfs_fh		*old_dir;
	const struct nfs_fh		*new_dir;
	const struct qstr		*old_name;
	const struct qstr		*new_name;
	const u32			*bitmask;
	struct nfs4_sequence_args	seq_args;
};

struct nfs_renameres {
	const struct nfs_server		*server;
	struct nfs4_change_info		old_cinfo;
	struct nfs_fattr		*old_fattr;
	struct nfs4_change_info		new_cinfo;
	struct nfs_fattr		*new_fattr;
	struct nfs4_sequence_res	seq_res;
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
	unsigned char		d_type;
	struct nfs_server *	server;
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

struct nfs_setattrargs {
	struct nfs_fh *                 fh;
	nfs4_stateid                    stateid;
	struct iattr *                  iap;
	const struct nfs_server *	server; /* Needed for name mapping */
	const u32 *			bitmask;
	struct nfs4_sequence_args 	seq_args;
};

struct nfs_setaclargs {
	struct nfs_fh *			fh;
	size_t				acl_len;
	unsigned int			acl_pgbase;
	struct page **			acl_pages;
	struct nfs4_sequence_args	seq_args;
};

struct nfs_setaclres {
	struct nfs4_sequence_res	seq_res;
};

struct nfs_getaclargs {
	struct nfs_fh *			fh;
	size_t				acl_len;
	unsigned int			acl_pgbase;
	struct page **			acl_pages;
	struct nfs4_sequence_args 	seq_args;
};

/* getxattr ACL interface flags */
#define NFS4_ACL_LEN_REQUEST	0x0001	/* zero length getxattr buffer */
struct nfs_getaclres {
	size_t				acl_len;
	size_t				acl_data_offset;
	int				acl_flags;
	struct page *			acl_scratch;
	struct nfs4_sequence_res	seq_res;
};

struct nfs_setattrres {
	struct nfs_fattr *              fattr;
	const struct nfs_server *	server;
	struct nfs4_sequence_res	seq_res;
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
	size_t			len;
	unsigned int		npages;
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
	struct nfs4_sequence_args	seq_args;
};

struct nfs4_accessres {
	const struct nfs_server *	server;
	struct nfs_fattr *		fattr;
	u32				supported;
	u32				access;
	struct nfs4_sequence_res	seq_res;
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
	struct nfs4_sequence_args 	seq_args;
};

struct nfs4_create_res {
	const struct nfs_server *	server;
	struct nfs_fh *			fh;
	struct nfs_fattr *		fattr;
	struct nfs4_change_info		dir_cinfo;
	struct nfs_fattr *		dir_fattr;
	struct nfs4_sequence_res	seq_res;
};

struct nfs4_fsinfo_arg {
	const struct nfs_fh *		fh;
	const u32 *			bitmask;
	struct nfs4_sequence_args	seq_args;
};

struct nfs4_fsinfo_res {
	struct nfs_fsinfo	       *fsinfo;
	struct nfs4_sequence_res	seq_res;
};

struct nfs4_getattr_arg {
	const struct nfs_fh *		fh;
	const u32 *			bitmask;
	struct nfs4_sequence_args	seq_args;
};

struct nfs4_getattr_res {
	const struct nfs_server *	server;
	struct nfs_fattr *		fattr;
	struct nfs4_sequence_res	seq_res;
};

struct nfs4_link_arg {
	const struct nfs_fh *		fh;
	const struct nfs_fh *		dir_fh;
	const struct qstr *		name;
	const u32 *			bitmask;
	struct nfs4_sequence_args 	seq_args;
};

struct nfs4_link_res {
	const struct nfs_server *	server;
	struct nfs_fattr *		fattr;
	struct nfs4_change_info		cinfo;
	struct nfs_fattr *		dir_attr;
	struct nfs4_sequence_res	seq_res;
};


struct nfs4_lookup_arg {
	const struct nfs_fh *		dir_fh;
	const struct qstr *		name;
	const u32 *			bitmask;
	struct nfs4_sequence_args	seq_args;
};

struct nfs4_lookup_res {
	const struct nfs_server *	server;
	struct nfs_fattr *		fattr;
	struct nfs_fh *			fh;
	struct nfs4_sequence_res	seq_res;
};

struct nfs4_lookup_root_arg {
	const u32 *			bitmask;
	struct nfs4_sequence_args	seq_args;
};

struct nfs4_pathconf_arg {
	const struct nfs_fh *		fh;
	const u32 *			bitmask;
	struct nfs4_sequence_args	seq_args;
};

struct nfs4_pathconf_res {
	struct nfs_pathconf	       *pathconf;
	struct nfs4_sequence_res	seq_res;
};

struct nfs4_readdir_arg {
	const struct nfs_fh *		fh;
	u64				cookie;
	nfs4_verifier			verifier;
	u32				count;
	struct page **			pages;	/* zero-copy data */
	unsigned int			pgbase;	/* zero-copy data */
	const u32 *			bitmask;
	int				plus;
	struct nfs4_sequence_args	seq_args;
};

struct nfs4_readdir_res {
	nfs4_verifier			verifier;
	unsigned int			pgbase;
	struct nfs4_sequence_res	seq_res;
};

struct nfs4_readlink {
	const struct nfs_fh *		fh;
	unsigned int			pgbase;
	unsigned int			pglen;   /* zero-copy data */
	struct page **			pages;   /* zero-copy data */
	struct nfs4_sequence_args	seq_args;
};

struct nfs4_readlink_res {
	struct nfs4_sequence_res	seq_res;
};

#define NFS4_SETCLIENTID_NAMELEN	(127)
struct nfs4_setclientid {
	const nfs4_verifier *		sc_verifier;
	unsigned int			sc_name_len;
	char				sc_name[NFS4_SETCLIENTID_NAMELEN + 1];
	u32				sc_prog;
	unsigned int			sc_netid_len;
	char				sc_netid[RPCBIND_MAXNETIDLEN + 1];
	unsigned int			sc_uaddr_len;
	char				sc_uaddr[RPCBIND_MAXUADDRLEN + 1];
	u32				sc_cb_ident;
};

struct nfs4_setclientid_res {
	u64				clientid;
	nfs4_verifier			confirm;
};

struct nfs4_statfs_arg {
	const struct nfs_fh *		fh;
	const u32 *			bitmask;
	struct nfs4_sequence_args	seq_args;
};

struct nfs4_statfs_res {
	struct nfs_fsstat	       *fsstat;
	struct nfs4_sequence_res	seq_res;
};

struct nfs4_server_caps_arg {
	struct nfs_fh		       *fhandle;
	struct nfs4_sequence_args	seq_args;
};

struct nfs4_server_caps_res {
	u32				attr_bitmask[3];
	u32				acl_bitmask;
	u32				has_links;
	u32				has_symlinks;
	u32				fh_expire_type;
	struct nfs4_sequence_res	seq_res;
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
	struct nfs4_sequence_args	seq_args;
};

struct nfs4_fs_locations_res {
	struct nfs4_fs_locations       *fs_locations;
	struct nfs4_sequence_res	seq_res;
};

struct nfs4_secinfo_oid {
	unsigned int len;
	char data[GSS_OID_MAX_LEN];
};

struct nfs4_secinfo_gss {
	struct nfs4_secinfo_oid sec_oid4;
	unsigned int qop4;
	unsigned int service;
};

struct nfs4_secinfo_flavor {
	unsigned int 		flavor;
	struct nfs4_secinfo_gss	gss;
};

struct nfs4_secinfo_flavors {
	unsigned int num_flavors;
	struct nfs4_secinfo_flavor flavors[0];
};

struct nfs4_secinfo_arg {
	const struct nfs_fh		*dir_fh;
	const struct qstr		*name;
	struct nfs4_sequence_args	seq_args;
};

struct nfs4_secinfo_res {
	struct nfs4_secinfo_flavors	*flavors;
	struct nfs4_sequence_res	seq_res;
};

#endif /* CONFIG_NFS_V4 */

struct nfstime4 {
	u64	seconds;
	u32	nseconds;
};

#ifdef CONFIG_NFS_V4_1
#define NFS4_EXCHANGE_ID_LEN	(48)
struct nfs41_exchange_id_args {
	struct nfs_client		*client;
	nfs4_verifier			*verifier;
	unsigned int 			id_len;
	char 				id[NFS4_EXCHANGE_ID_LEN];
	u32				flags;
};

struct server_owner {
	uint64_t			minor_id;
	uint32_t			major_id_sz;
	char				major_id[NFS4_OPAQUE_LIMIT];
};

struct server_scope {
	uint32_t			server_scope_sz;
	char 				server_scope[NFS4_OPAQUE_LIMIT];
};

struct nfs41_impl_id {
	char				domain[NFS4_OPAQUE_LIMIT + 1];
	char				name[NFS4_OPAQUE_LIMIT + 1];
	struct nfstime4			date;
};

struct nfs41_exchange_id_res {
	struct nfs_client		*client;
	u32				flags;
	struct server_scope		*server_scope;
	struct nfs41_impl_id		*impl_id;
};

struct nfs41_create_session_args {
	struct nfs_client	       *client;
	uint32_t			flags;
	uint32_t			cb_program;
	struct nfs4_channel_attrs	fc_attrs;	/* Fore Channel */
	struct nfs4_channel_attrs	bc_attrs;	/* Back Channel */
};

struct nfs41_create_session_res {
	struct nfs_client	       *client;
};

struct nfs41_reclaim_complete_args {
	/* In the future extend to include curr_fh for use with migration */
	unsigned char			one_fs:1;
	struct nfs4_sequence_args	seq_args;
};

struct nfs41_reclaim_complete_res {
	struct nfs4_sequence_res	seq_res;
};

#define SECINFO_STYLE_CURRENT_FH 0
#define SECINFO_STYLE_PARENT 1
struct nfs41_secinfo_no_name_args {
	int				style;
	struct nfs4_sequence_args	seq_args;
};

struct nfs41_test_stateid_args {
	nfs4_stateid			*stateid;
	struct nfs4_sequence_args	seq_args;
};

struct nfs41_test_stateid_res {
	unsigned int			status;
	struct nfs4_sequence_res	seq_res;
};

struct nfs41_free_stateid_args {
	nfs4_stateid			*stateid;
	struct nfs4_sequence_args	seq_args;
};

struct nfs41_free_stateid_res {
	unsigned int			status;
	struct nfs4_sequence_res	seq_res;
};

#endif /* CONFIG_NFS_V4_1 */

struct nfs_page;

#define NFS_PAGEVEC_SIZE	(8U)

struct nfs_read_data {
	struct rpc_task		task;
	struct inode		*inode;
	struct rpc_cred		*cred;
	struct nfs_fattr	fattr;	/* fattr storage */
	struct list_head	pages;	/* Coalesced read requests */
	struct list_head	list;	/* lists of struct nfs_read_data */
	struct nfs_page		*req;	/* multi ops per nfs_page */
	struct page		**pagevec;
	unsigned int		npages;	/* Max length of pagevec */
	struct nfs_readargs args;
	struct nfs_readres  res;
	unsigned long		timestamp;	/* For lease renewal */
	struct pnfs_layout_segment *lseg;
	struct nfs_client	*ds_clp;	/* pNFS data server */
	const struct rpc_call_ops *mds_ops;
	int (*read_done_cb) (struct rpc_task *task, struct nfs_read_data *data);
	__u64			mds_offset;
	int			pnfs_error;
	struct page		*page_array[NFS_PAGEVEC_SIZE];
};

struct nfs_write_data {
	struct rpc_task		task;
	struct inode		*inode;
	struct rpc_cred		*cred;
	struct nfs_fattr	fattr;
	struct nfs_writeverf	verf;
	struct list_head	pages;		/* Coalesced requests we wish to flush */
	struct list_head	list;		/* lists of struct nfs_write_data */
	struct nfs_page		*req;		/* multi ops per nfs_page */
	struct page		**pagevec;
	unsigned int		npages;		/* Max length of pagevec */
	struct nfs_writeargs	args;		/* argument struct */
	struct nfs_writeres	res;		/* result struct */
	struct pnfs_layout_segment *lseg;
	struct nfs_client	*ds_clp;	/* pNFS data server */
	int			ds_commit_index;
	const struct rpc_call_ops *mds_ops;
	int (*write_done_cb) (struct rpc_task *task, struct nfs_write_data *data);
#ifdef CONFIG_NFS_V4
	unsigned long		timestamp;	/* For lease renewal */
#endif
	__u64			mds_offset;	/* Filelayout dense stripe */
	int			pnfs_error;
	struct page		*page_array[NFS_PAGEVEC_SIZE];
};

struct nfs_unlinkdata {
	struct hlist_node list;
	struct nfs_removeargs args;
	struct nfs_removeres res;
	struct inode *dir;
	struct rpc_cred	*cred;
	struct nfs_fattr dir_attr;
};

struct nfs_renamedata {
	struct nfs_renameargs	args;
	struct nfs_renameres	res;
	struct rpc_cred		*cred;
	struct inode		*old_dir;
	struct dentry		*old_dentry;
	struct nfs_fattr	old_fattr;
	struct inode		*new_dir;
	struct dentry		*new_dentry;
	struct nfs_fattr	new_fattr;
};

struct nfs_access_entry;
struct nfs_client;
struct rpc_timeout;

/*
 * RPC procedure vector for NFSv2/NFSv3 demuxing
 */
struct nfs_rpc_ops {
	u32	version;		/* Protocol version */
	const struct dentry_operations *dentry_ops;
	const struct inode_operations *dir_inode_ops;
	const struct inode_operations *file_inode_ops;
	const struct file_operations *file_ops;

	int	(*getroot) (struct nfs_server *, struct nfs_fh *,
			    struct nfs_fsinfo *);
	int	(*getattr) (struct nfs_server *, struct nfs_fh *,
			    struct nfs_fattr *);
	int	(*setattr) (struct dentry *, struct nfs_fattr *,
			    struct iattr *);
	int	(*lookup)  (struct rpc_clnt *clnt, struct inode *, struct qstr *,
			    struct nfs_fh *, struct nfs_fattr *);
	int	(*access)  (struct inode *, struct nfs_access_entry *);
	int	(*readlink)(struct inode *, struct page *, unsigned int,
			    unsigned int);
	int	(*create)  (struct inode *, struct dentry *,
			    struct iattr *, int, struct nfs_open_context *);
	int	(*remove)  (struct inode *, struct qstr *);
	void	(*unlink_setup)  (struct rpc_message *, struct inode *dir);
	void	(*unlink_rpc_prepare) (struct rpc_task *, struct nfs_unlinkdata *);
	int	(*unlink_done) (struct rpc_task *, struct inode *);
	int	(*rename)  (struct inode *, struct qstr *,
			    struct inode *, struct qstr *);
	void	(*rename_setup)  (struct rpc_message *msg, struct inode *dir);
	void	(*rename_rpc_prepare)(struct rpc_task *task, struct nfs_renamedata *);
	int	(*rename_done) (struct rpc_task *task, struct inode *old_dir, struct inode *new_dir);
	int	(*link)    (struct inode *, struct inode *, struct qstr *);
	int	(*symlink) (struct inode *, struct dentry *, struct page *,
			    unsigned int, struct iattr *);
	int	(*mkdir)   (struct inode *, struct dentry *, struct iattr *);
	int	(*rmdir)   (struct inode *, struct qstr *);
	int	(*readdir) (struct dentry *, struct rpc_cred *,
			    u64, struct page **, unsigned int, int);
	int	(*mknod)   (struct inode *, struct dentry *, struct iattr *,
			    dev_t);
	int	(*statfs)  (struct nfs_server *, struct nfs_fh *,
			    struct nfs_fsstat *);
	int	(*fsinfo)  (struct nfs_server *, struct nfs_fh *,
			    struct nfs_fsinfo *);
	int	(*pathconf) (struct nfs_server *, struct nfs_fh *,
			     struct nfs_pathconf *);
	int	(*set_capabilities)(struct nfs_server *, struct nfs_fh *);
	int	(*decode_dirent)(struct xdr_stream *, struct nfs_entry *, int);
	void	(*read_setup)   (struct nfs_read_data *, struct rpc_message *);
	void	(*read_rpc_prepare)(struct rpc_task *, struct nfs_read_data *);
	int	(*read_done)  (struct rpc_task *, struct nfs_read_data *);
	void	(*write_setup)  (struct nfs_write_data *, struct rpc_message *);
	void	(*write_rpc_prepare)(struct rpc_task *, struct nfs_write_data *);
	int	(*write_done)  (struct rpc_task *, struct nfs_write_data *);
	void	(*commit_setup) (struct nfs_write_data *, struct rpc_message *);
	int	(*commit_done) (struct rpc_task *, struct nfs_write_data *);
	int	(*lock)(struct file *, int, struct file_lock *);
	int	(*lock_check_bounds)(const struct file_lock *);
	void	(*clear_acl_cache)(struct inode *);
	void	(*close_context)(struct nfs_open_context *ctx, int);
	struct inode * (*open_context) (struct inode *dir,
				struct nfs_open_context *ctx,
				int open_flags,
				struct iattr *iattr);
	int	(*init_client) (struct nfs_client *, const struct rpc_timeout *,
				const char *, rpc_authflavor_t, int);
	int	(*secinfo)(struct inode *, const struct qstr *, struct nfs4_secinfo_flavors *);
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
extern const struct rpc_version nfs_version2;
extern const struct rpc_version nfs_version3;
extern const struct rpc_version nfs_version4;

extern const struct rpc_version nfsacl_version3;
extern const struct rpc_program nfsacl_program;

#endif
