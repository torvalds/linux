/* SPDX-License-Identifier: GPL-2.0 */
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

#define NFS_BITMASK_SZ		3

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

struct nfs4_threshold {
	__u32	bm;
	__u32	l_type;
	__u64	rd_sz;
	__u64	wr_sz;
	__u64	rd_io_sz;
	__u64	wr_io_sz;
};

struct nfs_fattr {
	unsigned int		valid;		/* which fields are valid */
	umode_t			mode;
	__u32			nlink;
	kuid_t			uid;
	kgid_t			gid;
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
	struct timespec64	atime;
	struct timespec64	mtime;
	struct timespec64	ctime;
	__u64			change_attr;	/* NFSv4 change attribute */
	__u64			pre_change_attr;/* pre-op NFSv4 change attribute */
	__u64			pre_size;	/* pre_op_attr.size	  */
	struct timespec64	pre_mtime;	/* pre_op_attr.mtime	  */
	struct timespec64	pre_ctime;	/* pre_op_attr.ctime	  */
	unsigned long		time_start;
	unsigned long		gencount;
	struct nfs4_string	*owner_name;
	struct nfs4_string	*group_name;
	struct nfs4_threshold	*mdsthreshold;	/* pNFS threshold hints */
	struct nfs4_label	*label;
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
#define NFS_ATTR_FATTR_V4_SECURITY_LABEL (1U << 25)

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
		| NFS_ATTR_FATTR_CTIME \
		| NFS_ATTR_FATTR_CHANGE)
#define NFS_ATTR_FATTR_V2 (NFS_ATTR_FATTR \
		| NFS_ATTR_FATTR_BLOCKS_USED)
#define NFS_ATTR_FATTR_V3 (NFS_ATTR_FATTR \
		| NFS_ATTR_FATTR_SPACE_USED)
#define NFS_ATTR_FATTR_V4 (NFS_ATTR_FATTR \
		| NFS_ATTR_FATTR_SPACE_USED \
		| NFS_ATTR_FATTR_V4_SECURITY_LABEL)

/*
 * Maximal number of supported layout drivers.
 */
#define NFS_MAX_LAYOUT_TYPES 8

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
	struct timespec64	time_delta; /* server time granularity */
	__u32			lease_time; /* in seconds */
	__u32			nlayouttypes; /* number of layouttypes */
	__u32			layouttype[NFS_MAX_LAYOUT_TYPES]; /* supported pnfs layout driver */
	__u32			blksize; /* preferred pnfs io block size */
	__u32			clone_blksize; /* granularity of a CLONE operation */
	enum nfs4_change_attr_type
				change_attr_type; /* Info about change attr */
	__u32			xattr_support; /* User xattrs supported */
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

struct nfs4_slot;
struct nfs4_sequence_args {
	struct nfs4_slot	*sa_slot;
	u8			sa_cache_this : 1,
				sa_privileged : 1;
};

struct nfs4_sequence_res {
	struct nfs4_slot	*sr_slot;	/* slot used to send request */
	unsigned long		sr_timestamp;
	int			sr_status;	/* sequence operation status */
	u32			sr_status_flags;
	u32			sr_highest_slotid;
	u32			sr_target_highest_slotid;
};

struct nfs4_get_lease_time_args {
	struct nfs4_sequence_args	la_seq_args;
};

struct nfs4_get_lease_time_res {
	struct nfs4_sequence_res	lr_seq_res;
	struct nfs_fsinfo	       *lr_fsinfo;
};

struct xdr_stream;
struct nfs4_xdr_opaque_data;

struct nfs4_xdr_opaque_ops {
	void (*encode)(struct xdr_stream *, const void *args,
			const struct nfs4_xdr_opaque_data *);
	void (*free)(struct nfs4_xdr_opaque_data *);
};

struct nfs4_xdr_opaque_data {
	const struct nfs4_xdr_opaque_ops *ops;
	void *data;
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
	struct nfs4_sequence_args seq_args;
	__u32 type;
	struct pnfs_layout_range range;
	__u64 minlength;
	__u32 maxcount;
	struct inode *inode;
	struct nfs_open_context *ctx;
	nfs4_stateid stateid;
	struct nfs4_layoutdriver_data layout;
};

struct nfs4_layoutget_res {
	struct nfs4_sequence_res seq_res;
	int status;
	__u32 return_on_close;
	struct pnfs_layout_range range;
	__u32 type;
	nfs4_stateid stateid;
	struct nfs4_layoutdriver_data *layoutp;
};

struct nfs4_layoutget {
	struct nfs4_layoutget_args args;
	struct nfs4_layoutget_res res;
	const struct cred *cred;
	gfp_t gfp_flags;
};

struct nfs4_getdeviceinfo_args {
	struct nfs4_sequence_args seq_args;
	struct pnfs_device *pdev;
	__u32 notify_types;
};

struct nfs4_getdeviceinfo_res {
	struct nfs4_sequence_res seq_res;
	struct pnfs_device *pdev;
	__u32 notification;
};

struct nfs4_layoutcommit_args {
	struct nfs4_sequence_args seq_args;
	nfs4_stateid stateid;
	__u64 lastbytewritten;
	struct inode *inode;
	const u32 *bitmask;
	size_t layoutupdate_len;
	struct page *layoutupdate_page;
	struct page **layoutupdate_pages;
	__be32 *start_p;
};

struct nfs4_layoutcommit_res {
	struct nfs4_sequence_res seq_res;
	struct nfs_fattr *fattr;
	const struct nfs_server *server;
	int status;
};

struct nfs4_layoutcommit_data {
	struct rpc_task task;
	struct nfs_fattr fattr;
	struct list_head lseg_list;
	const struct cred *cred;
	struct inode *inode;
	struct nfs4_layoutcommit_args args;
	struct nfs4_layoutcommit_res res;
};

struct nfs4_layoutreturn_args {
	struct nfs4_sequence_args seq_args;
	struct pnfs_layout_hdr *layout;
	struct inode *inode;
	struct pnfs_layout_range range;
	nfs4_stateid stateid;
	__u32   layout_type;
	struct nfs4_xdr_opaque_data *ld_private;
};

struct nfs4_layoutreturn_res {
	struct nfs4_sequence_res seq_res;
	u32 lrs_present;
	nfs4_stateid stateid;
};

struct nfs4_layoutreturn {
	struct nfs4_layoutreturn_args args;
	struct nfs4_layoutreturn_res res;
	const struct cred *cred;
	struct nfs_client *clp;
	struct inode *inode;
	int rpc_status;
	struct nfs4_xdr_opaque_data ld_private;
};

#define PNFS_LAYOUTSTATS_MAXSIZE 256

struct nfs42_layoutstat_args;
struct nfs42_layoutstat_devinfo;
typedef	void (*layoutstats_encode_t)(struct xdr_stream *,
		struct nfs42_layoutstat_args *,
		struct nfs42_layoutstat_devinfo *);

/* Per file per deviceid layoutstats */
struct nfs42_layoutstat_devinfo {
	struct nfs4_deviceid dev_id;
	__u64 offset;
	__u64 length;
	__u64 read_count;
	__u64 read_bytes;
	__u64 write_count;
	__u64 write_bytes;
	__u32 layout_type;
	struct nfs4_xdr_opaque_data ld_private;
};

struct nfs42_layoutstat_args {
	struct nfs4_sequence_args seq_args;
	struct nfs_fh *fh;
	struct inode *inode;
	nfs4_stateid stateid;
	int num_dev;
	struct nfs42_layoutstat_devinfo *devinfo;
};

struct nfs42_layoutstat_res {
	struct nfs4_sequence_res seq_res;
	int num_dev;
	int rpc_status;
};

struct nfs42_layoutstat_data {
	struct inode *inode;
	struct nfs42_layoutstat_args args;
	struct nfs42_layoutstat_res res;
};

struct nfs42_device_error {
	struct nfs4_deviceid dev_id;
	int status;
	enum nfs_opnum4 opnum;
};

struct nfs42_layout_error {
	__u64 offset;
	__u64 length;
	nfs4_stateid stateid;
	struct nfs42_device_error errors[1];
};

#define NFS42_LAYOUTERROR_MAX 5

struct nfs42_layouterror_args {
	struct nfs4_sequence_args seq_args;
	struct inode *inode;
	unsigned int num_errors;
	struct nfs42_layout_error errors[NFS42_LAYOUTERROR_MAX];
};

struct nfs42_layouterror_res {
	struct nfs4_sequence_res seq_res;
	unsigned int num_errors;
	int rpc_status;
};

struct nfs42_layouterror_data {
	struct nfs42_layouterror_args args;
	struct nfs42_layouterror_res res;
	struct inode *inode;
	struct pnfs_layout_segment *lseg;
};

struct nfs42_clone_args {
	struct nfs4_sequence_args	seq_args;
	struct nfs_fh			*src_fh;
	struct nfs_fh			*dst_fh;
	nfs4_stateid			src_stateid;
	nfs4_stateid			dst_stateid;
	__u64				src_offset;
	__u64				dst_offset;
	__u64				count;
	const u32			*dst_bitmask;
};

struct nfs42_clone_res {
	struct nfs4_sequence_res	seq_res;
	unsigned int			rpc_status;
	struct nfs_fattr		*dst_fattr;
	const struct nfs_server		*server;
};

struct stateowner_id {
	__u64	create_time;
	__u32	uniquifier;
};

/*
 * Arguments to the open call.
 */
struct nfs_openargs {
	struct nfs4_sequence_args	seq_args;
	const struct nfs_fh *	fh;
	struct nfs_seqid *	seqid;
	int			open_flags;
	fmode_t			fmode;
	u32			share_access;
	u32			access;
	__u64                   clientid;
	struct stateowner_id	id;
	union {
		struct {
			struct iattr *  attrs;    /* UNCHECKED, GUARDED, EXCLUSIVE4_1 */
			nfs4_verifier   verifier; /* EXCLUSIVE */
		};
		nfs4_stateid	delegation;		/* CLAIM_DELEGATE_CUR */
		fmode_t		delegation_type;	/* CLAIM_PREVIOUS */
	} u;
	const struct qstr *	name;
	const struct nfs_server *server;	 /* Needed for ID mapping */
	const u32 *		bitmask;
	const u32 *		open_bitmap;
	enum open_claim_type4	claim;
	enum createmode4	createmode;
	const struct nfs4_label *label;
	umode_t			umask;
	struct nfs4_layoutget_args *lg_args;
};

struct nfs_openres {
	struct nfs4_sequence_res	seq_res;
	nfs4_stateid            stateid;
	struct nfs_fh           fh;
	struct nfs4_change_info	cinfo;
	__u32                   rflags;
	struct nfs_fattr *      f_attr;
	struct nfs4_label	*f_label;
	struct nfs_seqid *	seqid;
	const struct nfs_server *server;
	fmode_t			delegation_type;
	nfs4_stateid		delegation;
	unsigned long		pagemod_limit;
	__u32			do_recall;
	__u32			attrset[NFS4_BITMAP_SIZE];
	struct nfs4_string	*owner;
	struct nfs4_string	*group_owner;
	__u32			access_request;
	__u32			access_supported;
	__u32			access_result;
	struct nfs4_layoutget_res *lg_res;
};

/*
 * Arguments to the open_confirm call.
 */
struct nfs_open_confirmargs {
	struct nfs4_sequence_args	seq_args;
	const struct nfs_fh *	fh;
	nfs4_stateid *		stateid;
	struct nfs_seqid *	seqid;
};

struct nfs_open_confirmres {
	struct nfs4_sequence_res	seq_res;
	nfs4_stateid            stateid;
	struct nfs_seqid *	seqid;
};

/*
 * Arguments to the close call.
 */
struct nfs_closeargs {
	struct nfs4_sequence_args	seq_args;
	struct nfs_fh *         fh;
	nfs4_stateid 		stateid;
	struct nfs_seqid *	seqid;
	fmode_t			fmode;
	u32			share_access;
	const u32 *		bitmask;
	u32			bitmask_store[NFS_BITMASK_SZ];
	struct nfs4_layoutreturn_args *lr_args;
};

struct nfs_closeres {
	struct nfs4_sequence_res	seq_res;
	nfs4_stateid            stateid;
	struct nfs_fattr *	fattr;
	struct nfs_seqid *	seqid;
	const struct nfs_server *server;
	struct nfs4_layoutreturn_res *lr_res;
	int lr_ret;
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
	struct nfs4_sequence_args	seq_args;
	struct nfs_fh *		fh;
	struct file_lock *	fl;
	struct nfs_seqid *	lock_seqid;
	nfs4_stateid		lock_stateid;
	struct nfs_seqid *	open_seqid;
	nfs4_stateid		open_stateid;
	struct nfs_lowner	lock_owner;
	unsigned char		block : 1;
	unsigned char		reclaim : 1;
	unsigned char		new_lock : 1;
	unsigned char		new_lock_owner : 1;
};

struct nfs_lock_res {
	struct nfs4_sequence_res	seq_res;
	nfs4_stateid		stateid;
	struct nfs_seqid *	lock_seqid;
	struct nfs_seqid *	open_seqid;
};

struct nfs_locku_args {
	struct nfs4_sequence_args	seq_args;
	struct nfs_fh *		fh;
	struct file_lock *	fl;
	struct nfs_seqid *	seqid;
	nfs4_stateid 		stateid;
};

struct nfs_locku_res {
	struct nfs4_sequence_res	seq_res;
	nfs4_stateid		stateid;
	struct nfs_seqid *	seqid;
};

struct nfs_lockt_args {
	struct nfs4_sequence_args	seq_args;
	struct nfs_fh *		fh;
	struct file_lock *	fl;
	struct nfs_lowner	lock_owner;
};

struct nfs_lockt_res {
	struct nfs4_sequence_res	seq_res;
	struct file_lock *	denied; /* LOCK, LOCKT failed */
};

struct nfs_release_lockowner_args {
	struct nfs4_sequence_args	seq_args;
	struct nfs_lowner	lock_owner;
};

struct nfs_release_lockowner_res {
	struct nfs4_sequence_res	seq_res;
};

struct nfs4_delegreturnargs {
	struct nfs4_sequence_args	seq_args;
	const struct nfs_fh *fhandle;
	const nfs4_stateid *stateid;
	const u32 *bitmask;
	u32 bitmask_store[NFS_BITMASK_SZ];
	struct nfs4_layoutreturn_args *lr_args;
};

struct nfs4_delegreturnres {
	struct nfs4_sequence_res	seq_res;
	struct nfs_fattr * fattr;
	struct nfs_server *server;
	struct nfs4_layoutreturn_res *lr_res;
	int lr_ret;
};

/*
 * Arguments to the write call.
 */
struct nfs_write_verifier {
	char			data[8];
};

struct nfs_writeverf {
	struct nfs_write_verifier verifier;
	enum nfs3_stable_how	committed;
};

/*
 * Arguments shared by the read and write call.
 */
struct nfs_pgio_args {
	struct nfs4_sequence_args	seq_args;
	struct nfs_fh *		fh;
	struct nfs_open_context *context;
	struct nfs_lock_context *lock_context;
	nfs4_stateid		stateid;
	__u64			offset;
	__u32			count;
	unsigned int		pgbase;
	struct page **		pages;
	union {
		unsigned int		replen;			/* used by read */
		struct {
			const u32 *		bitmask;	/* used by write */
			u32 bitmask_store[NFS_BITMASK_SZ];	/* used by write */
			enum nfs3_stable_how	stable;		/* used by write */
		};
	};
};

struct nfs_pgio_res {
	struct nfs4_sequence_res	seq_res;
	struct nfs_fattr *	fattr;
	__u64			count;
	__u32			op_status;
	union {
		struct {
			unsigned int		replen;		/* used by read */
			int			eof;		/* used by read */
		};
		struct {
			struct nfs_writeverf *	verf;		/* used by write */
			const struct nfs_server *server;	/* used by write */
		};
	};
};

/*
 * Arguments to the commit call.
 */
struct nfs_commitargs {
	struct nfs4_sequence_args	seq_args;
	struct nfs_fh		*fh;
	__u64			offset;
	__u32			count;
	const u32		*bitmask;
};

struct nfs_commitres {
	struct nfs4_sequence_res	seq_res;
	__u32			op_status;
	struct nfs_fattr	*fattr;
	struct nfs_writeverf	*verf;
	const struct nfs_server *server;
};

/*
 * Common arguments to the unlink call
 */
struct nfs_removeargs {
	struct nfs4_sequence_args	seq_args;
	const struct nfs_fh	*fh;
	struct qstr		name;
};

struct nfs_removeres {
	struct nfs4_sequence_res 	seq_res;
	struct nfs_server *server;
	struct nfs_fattr	*dir_attr;
	struct nfs4_change_info	cinfo;
};

/*
 * Common arguments to the rename call
 */
struct nfs_renameargs {
	struct nfs4_sequence_args	seq_args;
	const struct nfs_fh		*old_dir;
	const struct nfs_fh		*new_dir;
	const struct qstr		*old_name;
	const struct qstr		*new_name;
};

struct nfs_renameres {
	struct nfs4_sequence_res	seq_res;
	struct nfs_server		*server;
	struct nfs4_change_info		old_cinfo;
	struct nfs_fattr		*old_fattr;
	struct nfs4_change_info		new_cinfo;
	struct nfs_fattr		*new_fattr;
};

/* parsed sec= options */
#define NFS_AUTH_INFO_MAX_FLAVORS 12 /* see fs/nfs/super.c */
struct nfs_auth_info {
	unsigned int            flavor_len;
	rpc_authflavor_t        flavors[NFS_AUTH_INFO_MAX_FLAVORS];
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
	struct nfs4_label  *label;
	unsigned char		d_type;
	struct nfs_server *	server;
};

struct nfs_readdir_arg {
	struct dentry		*dentry;
	const struct cred	*cred;
	__be32			*verf;
	u64			cookie;
	struct page		**pages;
	unsigned int		page_len;
	bool			plus;
};

struct nfs_readdir_res {
	__be32			*verf;
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
	struct nfs4_sequence_args 	seq_args;
	struct nfs_fh *                 fh;
	nfs4_stateid                    stateid;
	struct iattr *                  iap;
	const struct nfs_server *	server; /* Needed for name mapping */
	const u32 *			bitmask;
	const struct nfs4_label		*label;
};

struct nfs_setaclargs {
	struct nfs4_sequence_args	seq_args;
	struct nfs_fh *			fh;
	size_t				acl_len;
	struct page **			acl_pages;
};

struct nfs_setaclres {
	struct nfs4_sequence_res	seq_res;
};

struct nfs_getaclargs {
	struct nfs4_sequence_args 	seq_args;
	struct nfs_fh *			fh;
	size_t				acl_len;
	struct page **			acl_pages;
};

/* getxattr ACL interface flags */
#define NFS4_ACL_TRUNC		0x0001	/* ACL was truncated */
struct nfs_getaclres {
	struct nfs4_sequence_res	seq_res;
	size_t				acl_len;
	size_t				acl_data_offset;
	int				acl_flags;
	struct page *			acl_scratch;
};

struct nfs_setattrres {
	struct nfs4_sequence_res	seq_res;
	struct nfs_fattr *              fattr;
	struct nfs4_label		*label;
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
	struct timespec64	guardtime;
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
	bool			plus;
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
	bool			plus;
};

struct nfs3_getaclres {
	struct nfs_fattr *	fattr;
	int			mask;
	unsigned int		acl_access_count;
	unsigned int		acl_default_count;
	struct posix_acl *	acl_access;
	struct posix_acl *	acl_default;
};

#if IS_ENABLED(CONFIG_NFS_V4)

typedef u64 clientid4;

struct nfs4_accessargs {
	struct nfs4_sequence_args	seq_args;
	const struct nfs_fh *		fh;
	const u32 *			bitmask;
	u32				access;
};

struct nfs4_accessres {
	struct nfs4_sequence_res	seq_res;
	const struct nfs_server *	server;
	struct nfs_fattr *		fattr;
	u32				supported;
	u32				access;
};

struct nfs4_create_arg {
	struct nfs4_sequence_args 	seq_args;
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
	const struct nfs4_label		*label;
	umode_t				umask;
};

struct nfs4_create_res {
	struct nfs4_sequence_res	seq_res;
	const struct nfs_server *	server;
	struct nfs_fh *			fh;
	struct nfs_fattr *		fattr;
	struct nfs4_label		*label;
	struct nfs4_change_info		dir_cinfo;
};

struct nfs4_fsinfo_arg {
	struct nfs4_sequence_args	seq_args;
	const struct nfs_fh *		fh;
	const u32 *			bitmask;
};

struct nfs4_fsinfo_res {
	struct nfs4_sequence_res	seq_res;
	struct nfs_fsinfo	       *fsinfo;
};

struct nfs4_getattr_arg {
	struct nfs4_sequence_args	seq_args;
	const struct nfs_fh *		fh;
	const u32 *			bitmask;
};

struct nfs4_getattr_res {
	struct nfs4_sequence_res	seq_res;
	const struct nfs_server *	server;
	struct nfs_fattr *		fattr;
	struct nfs4_label		*label;
};

struct nfs4_link_arg {
	struct nfs4_sequence_args 	seq_args;
	const struct nfs_fh *		fh;
	const struct nfs_fh *		dir_fh;
	const struct qstr *		name;
	const u32 *			bitmask;
};

struct nfs4_link_res {
	struct nfs4_sequence_res	seq_res;
	const struct nfs_server *	server;
	struct nfs_fattr *		fattr;
	struct nfs4_label		*label;
	struct nfs4_change_info		cinfo;
	struct nfs_fattr *		dir_attr;
};

struct nfs4_lookup_arg {
	struct nfs4_sequence_args	seq_args;
	const struct nfs_fh *		dir_fh;
	const struct qstr *		name;
	const u32 *			bitmask;
};

struct nfs4_lookup_res {
	struct nfs4_sequence_res	seq_res;
	const struct nfs_server *	server;
	struct nfs_fattr *		fattr;
	struct nfs_fh *			fh;
	struct nfs4_label		*label;
};

struct nfs4_lookupp_arg {
	struct nfs4_sequence_args	seq_args;
	const struct nfs_fh		*fh;
	const u32			*bitmask;
};

struct nfs4_lookupp_res {
	struct nfs4_sequence_res	seq_res;
	const struct nfs_server		*server;
	struct nfs_fattr		*fattr;
	struct nfs_fh			*fh;
	struct nfs4_label		*label;
};

struct nfs4_lookup_root_arg {
	struct nfs4_sequence_args	seq_args;
	const u32 *			bitmask;
};

struct nfs4_pathconf_arg {
	struct nfs4_sequence_args	seq_args;
	const struct nfs_fh *		fh;
	const u32 *			bitmask;
};

struct nfs4_pathconf_res {
	struct nfs4_sequence_res	seq_res;
	struct nfs_pathconf	       *pathconf;
};

struct nfs4_readdir_arg {
	struct nfs4_sequence_args	seq_args;
	const struct nfs_fh *		fh;
	u64				cookie;
	nfs4_verifier			verifier;
	u32				count;
	struct page **			pages;	/* zero-copy data */
	unsigned int			pgbase;	/* zero-copy data */
	const u32 *			bitmask;
	bool				plus;
};

struct nfs4_readdir_res {
	struct nfs4_sequence_res	seq_res;
	nfs4_verifier			verifier;
	unsigned int			pgbase;
};

struct nfs4_readlink {
	struct nfs4_sequence_args	seq_args;
	const struct nfs_fh *		fh;
	unsigned int			pgbase;
	unsigned int			pglen;   /* zero-copy data */
	struct page **			pages;   /* zero-copy data */
};

struct nfs4_readlink_res {
	struct nfs4_sequence_res	seq_res;
};

struct nfs4_setclientid {
	const nfs4_verifier *		sc_verifier;
	u32				sc_prog;
	unsigned int			sc_netid_len;
	char				sc_netid[RPCBIND_MAXNETIDLEN + 1];
	unsigned int			sc_uaddr_len;
	char				sc_uaddr[RPCBIND_MAXUADDRLEN + 1];
	struct nfs_client		*sc_clnt;
	struct rpc_cred			*sc_cred;
};

struct nfs4_setclientid_res {
	u64				clientid;
	nfs4_verifier			confirm;
};

struct nfs4_statfs_arg {
	struct nfs4_sequence_args	seq_args;
	const struct nfs_fh *		fh;
	const u32 *			bitmask;
};

struct nfs4_statfs_res {
	struct nfs4_sequence_res	seq_res;
	struct nfs_fsstat	       *fsstat;
};

struct nfs4_server_caps_arg {
	struct nfs4_sequence_args	seq_args;
	struct nfs_fh		       *fhandle;
	const u32 *			bitmask;
};

struct nfs4_server_caps_res {
	struct nfs4_sequence_res	seq_res;
	u32				attr_bitmask[3];
	u32				exclcreat_bitmask[3];
	u32				acl_bitmask;
	u32				has_links;
	u32				has_symlinks;
	u32				fh_expire_type;
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
	struct nfs4_sequence_args	seq_args;
	const struct nfs_fh *dir_fh;
	const struct nfs_fh *fh;
	const struct qstr *name;
	struct page *page;
	const u32 *bitmask;
	clientid4 clientid;
	unsigned char migration:1, renew:1;
};

struct nfs4_fs_locations_res {
	struct nfs4_sequence_res	seq_res;
	struct nfs4_fs_locations       *fs_locations;
	unsigned char			migration:1, renew:1;
};

struct nfs4_secinfo4 {
	u32			flavor;
	struct rpcsec_gss_info	flavor_info;
};

struct nfs4_secinfo_flavors {
	unsigned int		num_flavors;
	struct nfs4_secinfo4	flavors[];
};

struct nfs4_secinfo_arg {
	struct nfs4_sequence_args	seq_args;
	const struct nfs_fh		*dir_fh;
	const struct qstr		*name;
};

struct nfs4_secinfo_res {
	struct nfs4_sequence_res	seq_res;
	struct nfs4_secinfo_flavors	*flavors;
};

struct nfs4_fsid_present_arg {
	struct nfs4_sequence_args	seq_args;
	const struct nfs_fh		*fh;
	clientid4			clientid;
	unsigned char			renew:1;
};

struct nfs4_fsid_present_res {
	struct nfs4_sequence_res	seq_res;
	struct nfs_fh			*fh;
	unsigned char			renew:1;
};

#endif /* CONFIG_NFS_V4 */

struct nfstime4 {
	u64	seconds;
	u32	nseconds;
};

#ifdef CONFIG_NFS_V4_1

struct pnfs_commit_bucket {
	struct list_head written;
	struct list_head committing;
	struct pnfs_layout_segment *lseg;
	struct nfs_writeverf direct_verf;
};

struct pnfs_commit_array {
	struct list_head cinfo_list;
	struct list_head lseg_list;
	struct pnfs_layout_segment *lseg;
	struct rcu_head rcu;
	refcount_t refcount;
	unsigned int nbuckets;
	struct pnfs_commit_bucket buckets[];
};

struct pnfs_ds_commit_info {
	struct list_head commits;
	unsigned int nwritten;
	unsigned int ncommitting;
	const struct pnfs_commit_ops *ops;
};

struct nfs41_state_protection {
	u32 how;
	struct nfs4_op_map enforce;
	struct nfs4_op_map allow;
};

struct nfs41_exchange_id_args {
	struct nfs_client		*client;
	nfs4_verifier			verifier;
	u32				flags;
	struct nfs41_state_protection	state_protect;
};

struct nfs41_server_owner {
	uint64_t			minor_id;
	uint32_t			major_id_sz;
	char				major_id[NFS4_OPAQUE_LIMIT];
};

struct nfs41_server_scope {
	uint32_t			server_scope_sz;
	char 				server_scope[NFS4_OPAQUE_LIMIT];
};

struct nfs41_impl_id {
	char				domain[NFS4_OPAQUE_LIMIT + 1];
	char				name[NFS4_OPAQUE_LIMIT + 1];
	struct nfstime4			date;
};

#define MAX_BIND_CONN_TO_SESSION_RETRIES 3
struct nfs41_bind_conn_to_session_args {
	struct nfs_client		*client;
	struct nfs4_sessionid		sessionid;
	u32				dir;
	bool				use_conn_in_rdma_mode;
	int				retries;
};

struct nfs41_bind_conn_to_session_res {
	struct nfs4_sessionid		sessionid;
	u32				dir;
	bool				use_conn_in_rdma_mode;
};

struct nfs41_exchange_id_res {
	u64				clientid;
	u32				seqid;
	u32				flags;
	struct nfs41_server_owner	*server_owner;
	struct nfs41_server_scope	*server_scope;
	struct nfs41_impl_id		*impl_id;
	struct nfs41_state_protection	state_protect;
};

struct nfs41_create_session_args {
	struct nfs_client	       *client;
	u64				clientid;
	uint32_t			seqid;
	uint32_t			flags;
	uint32_t			cb_program;
	struct nfs4_channel_attrs	fc_attrs;	/* Fore Channel */
	struct nfs4_channel_attrs	bc_attrs;	/* Back Channel */
};

struct nfs41_create_session_res {
	struct nfs4_sessionid		sessionid;
	uint32_t			seqid;
	uint32_t			flags;
	struct nfs4_channel_attrs	fc_attrs;	/* Fore Channel */
	struct nfs4_channel_attrs	bc_attrs;	/* Back Channel */
};

struct nfs41_reclaim_complete_args {
	struct nfs4_sequence_args	seq_args;
	/* In the future extend to include curr_fh for use with migration */
	unsigned char			one_fs:1;
};

struct nfs41_reclaim_complete_res {
	struct nfs4_sequence_res	seq_res;
};

#define SECINFO_STYLE_CURRENT_FH 0
#define SECINFO_STYLE_PARENT 1
struct nfs41_secinfo_no_name_args {
	struct nfs4_sequence_args	seq_args;
	int				style;
};

struct nfs41_test_stateid_args {
	struct nfs4_sequence_args	seq_args;
	nfs4_stateid			*stateid;
};

struct nfs41_test_stateid_res {
	struct nfs4_sequence_res	seq_res;
	unsigned int			status;
};

struct nfs41_free_stateid_args {
	struct nfs4_sequence_args	seq_args;
	nfs4_stateid			stateid;
};

struct nfs41_free_stateid_res {
	struct nfs4_sequence_res	seq_res;
	unsigned int			status;
};

#else

struct pnfs_ds_commit_info {
};

#endif /* CONFIG_NFS_V4_1 */

#ifdef CONFIG_NFS_V4_2
struct nfs42_falloc_args {
	struct nfs4_sequence_args	seq_args;

	struct nfs_fh			*falloc_fh;
	nfs4_stateid			 falloc_stateid;
	u64				 falloc_offset;
	u64				 falloc_length;
	const u32			*falloc_bitmask;
};

struct nfs42_falloc_res {
	struct nfs4_sequence_res	seq_res;
	unsigned int			status;

	struct nfs_fattr		*falloc_fattr;
	const struct nfs_server		*falloc_server;
};

struct nfs42_copy_args {
	struct nfs4_sequence_args	seq_args;

	struct nfs_fh			*src_fh;
	nfs4_stateid			src_stateid;
	u64				src_pos;

	struct nfs_fh			*dst_fh;
	nfs4_stateid			dst_stateid;
	u64				dst_pos;

	u64				count;
	bool				sync;
	struct nl4_server		*cp_src;
};

struct nfs42_write_res {
	nfs4_stateid		stateid;
	u64			count;
	struct nfs_writeverf	verifier;
};

struct nfs42_copy_res {
	struct nfs4_sequence_res	seq_res;
	struct nfs42_write_res		write_res;
	bool				consecutive;
	bool				synchronous;
	struct nfs_commitres		commit_res;
};

struct nfs42_offload_status_args {
	struct nfs4_sequence_args	osa_seq_args;
	struct nfs_fh			*osa_src_fh;
	nfs4_stateid			osa_stateid;
};

struct nfs42_offload_status_res {
	struct nfs4_sequence_res	osr_seq_res;
	uint64_t			osr_count;
	int				osr_status;
};

struct nfs42_copy_notify_args {
	struct nfs4_sequence_args	cna_seq_args;

	struct nfs_fh		*cna_src_fh;
	nfs4_stateid		cna_src_stateid;
	struct nl4_server	cna_dst;
};

struct nfs42_copy_notify_res {
	struct nfs4_sequence_res	cnr_seq_res;

	struct nfstime4		cnr_lease_time;
	nfs4_stateid		cnr_stateid;
	struct nl4_server	cnr_src;
};

struct nfs42_seek_args {
	struct nfs4_sequence_args	seq_args;

	struct nfs_fh			*sa_fh;
	nfs4_stateid			sa_stateid;
	u64				sa_offset;
	u32				sa_what;
};

struct nfs42_seek_res {
	struct nfs4_sequence_res	seq_res;
	unsigned int			status;

	u32	sr_eof;
	u64	sr_offset;
};

struct nfs42_setxattrargs {
	struct nfs4_sequence_args	seq_args;
	struct nfs_fh			*fh;
	const char			*xattr_name;
	u32				xattr_flags;
	size_t				xattr_len;
	struct page			**xattr_pages;
};

struct nfs42_setxattrres {
	struct nfs4_sequence_res	seq_res;
	struct nfs4_change_info		cinfo;
};

struct nfs42_getxattrargs {
	struct nfs4_sequence_args	seq_args;
	struct nfs_fh			*fh;
	const char			*xattr_name;
	size_t				xattr_len;
	struct page			**xattr_pages;
};

struct nfs42_getxattrres {
	struct nfs4_sequence_res	seq_res;
	size_t				xattr_len;
};

struct nfs42_listxattrsargs {
	struct nfs4_sequence_args	seq_args;
	struct nfs_fh			*fh;
	u32				count;
	u64				cookie;
	struct page			**xattr_pages;
};

struct nfs42_listxattrsres {
	struct nfs4_sequence_res	seq_res;
	struct page			*scratch;
	void				*xattr_buf;
	size_t				xattr_len;
	u64				cookie;
	bool				eof;
	size_t				copied;
};

struct nfs42_removexattrargs {
	struct nfs4_sequence_args	seq_args;
	struct nfs_fh			*fh;
	const char			*xattr_name;
};

struct nfs42_removexattrres {
	struct nfs4_sequence_res	seq_res;
	struct nfs4_change_info		cinfo;
};

#endif /* CONFIG_NFS_V4_2 */

struct nfs_page;

#define NFS_PAGEVEC_SIZE	(8U)

struct nfs_page_array {
	struct page		**pagevec;
	unsigned int		npages;		/* Max length of pagevec */
	struct page		*page_array[NFS_PAGEVEC_SIZE];
};

/* used as flag bits in nfs_pgio_header */
enum {
	NFS_IOHDR_ERROR = 0,
	NFS_IOHDR_EOF,
	NFS_IOHDR_REDO,
	NFS_IOHDR_STAT,
	NFS_IOHDR_RESEND_PNFS,
	NFS_IOHDR_RESEND_MDS,
};

struct nfs_io_completion;
struct nfs_pgio_header {
	struct inode		*inode;
	const struct cred		*cred;
	struct list_head	pages;
	struct nfs_page		*req;
	struct nfs_writeverf	verf;		/* Used for writes */
	fmode_t			rw_mode;
	struct pnfs_layout_segment *lseg;
	loff_t			io_start;
	const struct rpc_call_ops *mds_ops;
	void (*release) (struct nfs_pgio_header *hdr);
	const struct nfs_pgio_completion_ops *completion_ops;
	const struct nfs_rw_ops	*rw_ops;
	struct nfs_io_completion *io_completion;
	struct nfs_direct_req	*dreq;

	int			pnfs_error;
	int			error;		/* merge with pnfs_error */
	unsigned int		good_bytes;	/* boundary of good data */
	unsigned long		flags;

	/*
	 * rpc data
	 */
	struct rpc_task		task;
	struct nfs_fattr	fattr;
	struct nfs_pgio_args	args;		/* argument struct */
	struct nfs_pgio_res	res;		/* result struct */
	unsigned long		timestamp;	/* For lease renewal */
	int (*pgio_done_cb)(struct rpc_task *, struct nfs_pgio_header *);
	__u64			mds_offset;	/* Filelayout dense stripe */
	struct nfs_page_array	page_array;
	struct nfs_client	*ds_clp;	/* pNFS data server */
	u32			ds_commit_idx;	/* ds index if ds_clp is set */
	u32			pgio_mirror_idx;/* mirror index in pgio layer */
};

struct nfs_mds_commit_info {
	atomic_t rpcs_out;
	atomic_long_t		ncommit;
	struct list_head	list;
};

struct nfs_commit_info;
struct nfs_commit_data;
struct nfs_inode;
struct nfs_commit_completion_ops {
	void (*completion) (struct nfs_commit_data *data);
	void (*resched_write) (struct nfs_commit_info *, struct nfs_page *);
};

struct nfs_commit_info {
	struct inode 			*inode;	/* Needed for inode->i_lock */
	struct nfs_mds_commit_info	*mds;
	struct pnfs_ds_commit_info	*ds;
	struct nfs_direct_req		*dreq;	/* O_DIRECT request */
	const struct nfs_commit_completion_ops *completion_ops;
};

struct nfs_commit_data {
	struct rpc_task		task;
	struct inode		*inode;
	const struct cred		*cred;
	struct nfs_fattr	fattr;
	struct nfs_writeverf	verf;
	struct list_head	pages;		/* Coalesced requests we wish to flush */
	struct list_head	list;		/* lists of struct nfs_write_data */
	struct nfs_direct_req	*dreq;		/* O_DIRECT request */
	struct nfs_commitargs	args;		/* argument struct */
	struct nfs_commitres	res;		/* result struct */
	struct nfs_open_context *context;
	struct pnfs_layout_segment *lseg;
	struct nfs_client	*ds_clp;	/* pNFS data server */
	int			ds_commit_index;
	loff_t			lwb;
	const struct rpc_call_ops *mds_ops;
	const struct nfs_commit_completion_ops *completion_ops;
	int (*commit_done_cb) (struct rpc_task *task, struct nfs_commit_data *data);
	unsigned long		flags;
};

struct nfs_pgio_completion_ops {
	void	(*error_cleanup)(struct list_head *head, int);
	void	(*init_hdr)(struct nfs_pgio_header *hdr);
	void	(*completion)(struct nfs_pgio_header *hdr);
	void	(*reschedule_io)(struct nfs_pgio_header *hdr);
};

struct nfs_unlinkdata {
	struct nfs_removeargs args;
	struct nfs_removeres res;
	struct dentry *dentry;
	wait_queue_head_t wq;
	const struct cred *cred;
	struct nfs_fattr dir_attr;
	long timeout;
};

struct nfs_renamedata {
	struct nfs_renameargs	args;
	struct nfs_renameres	res;
	const struct cred	*cred;
	struct inode		*old_dir;
	struct dentry		*old_dentry;
	struct nfs_fattr	old_fattr;
	struct inode		*new_dir;
	struct dentry		*new_dentry;
	struct nfs_fattr	new_fattr;
	void (*complete)(struct rpc_task *, struct nfs_renamedata *);
	long timeout;
	bool cancelled;
};

struct nfs_access_entry;
struct nfs_client;
struct rpc_timeout;
struct nfs_subversion;
struct nfs_mount_info;
struct nfs_client_initdata;
struct nfs_pageio_descriptor;
struct fs_context;

/*
 * RPC procedure vector for NFSv2/NFSv3 demuxing
 */
struct nfs_rpc_ops {
	u32	version;		/* Protocol version */
	const struct dentry_operations *dentry_ops;
	const struct inode_operations *dir_inode_ops;
	const struct inode_operations *file_inode_ops;
	const struct file_operations *file_ops;
	const struct nlmclnt_operations *nlmclnt_ops;

	int	(*getroot) (struct nfs_server *, struct nfs_fh *,
			    struct nfs_fsinfo *);
	int	(*submount) (struct fs_context *, struct nfs_server *);
	int	(*try_get_tree) (struct fs_context *);
	int	(*getattr) (struct nfs_server *, struct nfs_fh *,
			    struct nfs_fattr *, struct nfs4_label *,
			    struct inode *);
	int	(*setattr) (struct dentry *, struct nfs_fattr *,
			    struct iattr *);
	int	(*lookup)  (struct inode *, struct dentry *,
			    struct nfs_fh *, struct nfs_fattr *,
			    struct nfs4_label *);
	int	(*lookupp) (struct inode *, struct nfs_fh *,
			    struct nfs_fattr *, struct nfs4_label *);
	int	(*access)  (struct inode *, struct nfs_access_entry *);
	int	(*readlink)(struct inode *, struct page *, unsigned int,
			    unsigned int);
	int	(*create)  (struct inode *, struct dentry *,
			    struct iattr *, int);
	int	(*remove)  (struct inode *, struct dentry *);
	void	(*unlink_setup)  (struct rpc_message *, struct dentry *, struct inode *);
	void	(*unlink_rpc_prepare) (struct rpc_task *, struct nfs_unlinkdata *);
	int	(*unlink_done) (struct rpc_task *, struct inode *);
	void	(*rename_setup)  (struct rpc_message *msg,
			struct dentry *old_dentry,
			struct dentry *new_dentry);
	void	(*rename_rpc_prepare)(struct rpc_task *task, struct nfs_renamedata *);
	int	(*rename_done) (struct rpc_task *task, struct inode *old_dir, struct inode *new_dir);
	int	(*link)    (struct inode *, struct inode *, const struct qstr *);
	int	(*symlink) (struct inode *, struct dentry *, struct page *,
			    unsigned int, struct iattr *);
	int	(*mkdir)   (struct inode *, struct dentry *, struct iattr *);
	int	(*rmdir)   (struct inode *, const struct qstr *);
	int	(*readdir) (struct nfs_readdir_arg *, struct nfs_readdir_res *);
	int	(*mknod)   (struct inode *, struct dentry *, struct iattr *,
			    dev_t);
	int	(*statfs)  (struct nfs_server *, struct nfs_fh *,
			    struct nfs_fsstat *);
	int	(*fsinfo)  (struct nfs_server *, struct nfs_fh *,
			    struct nfs_fsinfo *);
	int	(*pathconf) (struct nfs_server *, struct nfs_fh *,
			     struct nfs_pathconf *);
	int	(*set_capabilities)(struct nfs_server *, struct nfs_fh *);
	int	(*decode_dirent)(struct xdr_stream *, struct nfs_entry *, bool);
	int	(*pgio_rpc_prepare)(struct rpc_task *,
				    struct nfs_pgio_header *);
	void	(*read_setup)(struct nfs_pgio_header *, struct rpc_message *);
	int	(*read_done)(struct rpc_task *, struct nfs_pgio_header *);
	void	(*write_setup)(struct nfs_pgio_header *, struct rpc_message *,
				struct rpc_clnt **);
	int	(*write_done)(struct rpc_task *, struct nfs_pgio_header *);
	void	(*commit_setup) (struct nfs_commit_data *, struct rpc_message *,
				struct rpc_clnt **);
	void	(*commit_rpc_prepare)(struct rpc_task *, struct nfs_commit_data *);
	int	(*commit_done) (struct rpc_task *, struct nfs_commit_data *);
	int	(*lock)(struct file *, int, struct file_lock *);
	int	(*lock_check_bounds)(const struct file_lock *);
	void	(*clear_acl_cache)(struct inode *);
	void	(*close_context)(struct nfs_open_context *ctx, int);
	struct inode * (*open_context) (struct inode *dir,
				struct nfs_open_context *ctx,
				int open_flags,
				struct iattr *iattr,
				int *);
	int (*have_delegation)(struct inode *, fmode_t);
	struct nfs_client *(*alloc_client) (const struct nfs_client_initdata *);
	struct nfs_client *(*init_client) (struct nfs_client *,
				const struct nfs_client_initdata *);
	void	(*free_client) (struct nfs_client *);
	struct nfs_server *(*create_server)(struct fs_context *);
	struct nfs_server *(*clone_server)(struct nfs_server *, struct nfs_fh *,
					   struct nfs_fattr *, rpc_authflavor_t);
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
