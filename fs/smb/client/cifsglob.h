/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Jeremy Allison (jra@samba.org)
 *
 */
#ifndef _CIFS_GLOB_H
#define _CIFS_GLOB_H

#include <linux/in.h>
#include <linux/in6.h>
#include <linux/inet.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/mm.h>
#include <linux/mempool.h>
#include <linux/workqueue.h>
#include <linux/utsname.h>
#include <linux/sched/mm.h>
#include <linux/netfs.h>
#include "cifs_fs_sb.h"
#include "cifsacl.h"
#include <crypto/internal/hash.h>
#include <uapi/linux/cifs/cifs_mount.h>
#include "../common/smb2pdu.h"
#include "smb2pdu.h"
#include <linux/filelock.h>

#define SMB_PATH_MAX 260
#define CIFS_PORT 445
#define RFC1001_PORT 139

/*
 * The sizes of various internal tables and strings
 */
#define MAX_UID_INFO 16
#define MAX_SES_INFO 2
#define MAX_TCON_INFO 4

#define MAX_TREE_SIZE (2 + CIFS_NI_MAXHOST + 1 + CIFS_MAX_SHARE_LEN + 1)

#define CIFS_MIN_RCV_POOL 4

#define MAX_REOPEN_ATT	5 /* these many maximum attempts to reopen a file */
/*
 * default attribute cache timeout (jiffies)
 */
#define CIFS_DEF_ACTIMEO (1 * HZ)

/*
 * max sleep time before retry to server
 */
#define CIFS_MAX_SLEEP 2000

/*
 * max attribute cache timeout (jiffies) - 2^30
 */
#define CIFS_MAX_ACTIMEO (1 << 30)

/*
 * Max persistent and resilient handle timeout (milliseconds).
 * Windows durable max was 960000 (16 minutes)
 */
#define SMB3_MAX_HANDLE_TIMEOUT 960000

/*
 * MAX_REQ is the maximum number of requests that WE will send
 * on one socket concurrently.
 */
#define CIFS_MAX_REQ 32767

#define RFC1001_NAME_LEN 15
#define RFC1001_NAME_LEN_WITH_NULL (RFC1001_NAME_LEN + 1)

/* maximum length of ip addr as a string (including ipv6 and sctp) */
#define SERVER_NAME_LENGTH 80
#define SERVER_NAME_LEN_WITH_NULL     (SERVER_NAME_LENGTH + 1)

/* echo interval in seconds */
#define SMB_ECHO_INTERVAL_MIN 1
#define SMB_ECHO_INTERVAL_MAX 600
#define SMB_ECHO_INTERVAL_DEFAULT 60

/* smb multichannel query server interfaces interval in seconds */
#define SMB_INTERFACE_POLL_INTERVAL	600

/* maximum number of PDUs in one compound */
#define MAX_COMPOUND 7

/*
 * Default number of credits to keep available for SMB3.
 * This value is chosen somewhat arbitrarily. The Windows client
 * defaults to 128 credits, the Windows server allows clients up to
 * 512 credits (or 8K for later versions), and the NetApp server
 * does not limit clients at all.  Choose a high enough default value
 * such that the client shouldn't limit performance, but allow mount
 * to override (until you approach 64K, where we limit credits to 65000
 * to reduce possibility of seeing more server credit overflow bugs.
 */
#define SMB2_MAX_CREDITS_AVAILABLE 32000

#include "cifspdu.h"

#ifndef XATTR_DOS_ATTRIB
#define XATTR_DOS_ATTRIB "user.DOSATTRIB"
#endif

#define CIFS_MAX_WORKSTATION_LEN  (__NEW_UTS_LEN + 1)  /* reasonable max for client */

#define CIFS_DFS_ROOT_SES(ses) ((ses)->dfs_root_ses ?: (ses))

/*
 * CIFS vfs client Status information (based on what we know.)
 */

/* associated with each connection */
enum statusEnum {
	CifsNew = 0,
	CifsGood,
	CifsExiting,
	CifsNeedReconnect,
	CifsNeedNegotiate,
	CifsInNegotiate,
};

/* associated with each smb session */
enum ses_status_enum {
	SES_NEW = 0,
	SES_GOOD,
	SES_EXITING,
	SES_NEED_RECON,
	SES_IN_SETUP
};

/* associated with each tree connection to the server */
enum tid_status_enum {
	TID_NEW = 0,
	TID_GOOD,
	TID_EXITING,
	TID_NEED_RECON,
	TID_NEED_TCON,
	TID_IN_TCON,
	TID_NEED_FILES_INVALIDATE, /* currently unused */
	TID_IN_FILES_INVALIDATE
};

enum securityEnum {
	Unspecified = 0,	/* not specified */
	NTLMv2,			/* Legacy NTLM auth with NTLMv2 hash */
	RawNTLMSSP,		/* NTLMSSP without SPNEGO, NTLMv2 hash */
	Kerberos,		/* Kerberos via SPNEGO */
};

enum cifs_reparse_type {
	CIFS_REPARSE_TYPE_NFS,
	CIFS_REPARSE_TYPE_WSL,
	CIFS_REPARSE_TYPE_DEFAULT = CIFS_REPARSE_TYPE_NFS,
};

static inline const char *cifs_reparse_type_str(enum cifs_reparse_type type)
{
	switch (type) {
	case CIFS_REPARSE_TYPE_NFS:
		return "nfs";
	case CIFS_REPARSE_TYPE_WSL:
		return "wsl";
	default:
		return "unknown";
	}
}

struct session_key {
	unsigned int len;
	char *response;
};

/* crypto hashing related structure/fields, not specific to a sec mech */
struct cifs_secmech {
	struct shash_desc *md5; /* md5 hash function, for CIFS/SMB1 signatures */
	struct shash_desc *hmacsha256; /* hmac-sha256 hash function, for SMB2 signatures */
	struct shash_desc *sha512; /* sha512 hash function, for SMB3.1.1 preauth hash */
	struct shash_desc *aes_cmac; /* block-cipher based MAC function, for SMB3 signatures */

	struct crypto_aead *enc; /* smb3 encryption AEAD TFM (AES-CCM and AES-GCM) */
	struct crypto_aead *dec; /* smb3 decryption AEAD TFM (AES-CCM and AES-GCM) */
};

/* per smb session structure/fields */
struct ntlmssp_auth {
	bool sesskey_per_smbsess; /* whether session key is per smb session */
	__u32 client_flags; /* sent by client in type 1 ntlmsssp exchange */
	__u32 server_flags; /* sent by server in type 2 ntlmssp exchange */
	unsigned char ciphertext[CIFS_CPHTXT_SIZE]; /* sent to server */
	char cryptkey[CIFS_CRYPTO_KEY_SIZE]; /* used by ntlmssp */
};

struct cifs_cred {
	int uid;
	int gid;
	int mode;
	int cecount;
	struct smb_sid osid;
	struct smb_sid gsid;
	struct cifs_ntace *ntaces;
	struct smb_ace *aces;
};

struct cifs_open_info_data {
	bool adjust_tz;
	union {
		bool reparse_point;
		bool symlink;
	};
	struct {
		/* ioctl response buffer */
		struct {
			int buftype;
			struct kvec iov;
		} io;
		__u32 tag;
		union {
			struct reparse_data_buffer *buf;
			struct reparse_posix_data *posix;
		};
	} reparse;
	struct {
		__u8		eas[SMB2_WSL_MAX_QUERY_EA_RESP_SIZE];
		unsigned int	eas_len;
	} wsl;
	char *symlink_target;
	struct smb_sid posix_owner;
	struct smb_sid posix_group;
	union {
		struct smb2_file_all_info fi;
		struct smb311_posix_qinfo posix_fi;
	};
};

/*
 *****************************************************************
 * Except the CIFS PDUs themselves all the
 * globally interesting structs should go here
 *****************************************************************
 */

/*
 * A smb_rqst represents a complete request to be issued to a server. It's
 * formed by a kvec array, followed by an array of pages. Page data is assumed
 * to start at the beginning of the first page.
 */
struct smb_rqst {
	struct kvec	*rq_iov;	/* array of kvecs */
	unsigned int	rq_nvec;	/* number of kvecs in array */
	struct iov_iter	rq_iter;	/* Data iterator */
	struct folio_queue *rq_buffer;	/* Buffer for encryption */
};

struct mid_q_entry;
struct TCP_Server_Info;
struct cifsFileInfo;
struct cifs_ses;
struct cifs_tcon;
struct dfs_info3_param;
struct cifs_fattr;
struct smb3_fs_context;
struct cifs_fid;
struct cifs_io_subrequest;
struct cifs_io_parms;
struct cifs_search_info;
struct cifsInodeInfo;
struct cifs_open_parms;
struct cifs_credits;

struct smb_version_operations {
	int (*send_cancel)(struct TCP_Server_Info *, struct smb_rqst *,
			   struct mid_q_entry *);
	bool (*compare_fids)(struct cifsFileInfo *, struct cifsFileInfo *);
	/* setup request: allocate mid, sign message */
	struct mid_q_entry *(*setup_request)(struct cifs_ses *,
					     struct TCP_Server_Info *,
					     struct smb_rqst *);
	/* setup async request: allocate mid, sign message */
	struct mid_q_entry *(*setup_async_request)(struct TCP_Server_Info *,
						struct smb_rqst *);
	/* check response: verify signature, map error */
	int (*check_receive)(struct mid_q_entry *, struct TCP_Server_Info *,
			     bool);
	void (*add_credits)(struct TCP_Server_Info *server,
			    struct cifs_credits *credits,
			    const int optype);
	void (*set_credits)(struct TCP_Server_Info *, const int);
	int * (*get_credits_field)(struct TCP_Server_Info *, const int);
	unsigned int (*get_credits)(struct mid_q_entry *);
	__u64 (*get_next_mid)(struct TCP_Server_Info *);
	void (*revert_current_mid)(struct TCP_Server_Info *server,
				   const unsigned int val);
	/* data offset from read response message */
	unsigned int (*read_data_offset)(char *);
	/*
	 * Data length from read response message
	 * When in_remaining is true, the returned data length is in
	 * message field DataRemaining for out-of-band data read (e.g through
	 * Memory Registration RDMA write in SMBD).
	 * Otherwise, the returned data length is in message field DataLength.
	 */
	unsigned int (*read_data_length)(char *, bool in_remaining);
	/* map smb to linux error */
	int (*map_error)(char *, bool);
	/* find mid corresponding to the response message */
	struct mid_q_entry * (*find_mid)(struct TCP_Server_Info *, char *);
	void (*dump_detail)(void *buf, struct TCP_Server_Info *ptcp_info);
	void (*clear_stats)(struct cifs_tcon *);
	void (*print_stats)(struct seq_file *m, struct cifs_tcon *);
	void (*dump_share_caps)(struct seq_file *, struct cifs_tcon *);
	/* verify the message */
	int (*check_message)(char *, unsigned int, struct TCP_Server_Info *);
	bool (*is_oplock_break)(char *, struct TCP_Server_Info *);
	int (*handle_cancelled_mid)(struct mid_q_entry *, struct TCP_Server_Info *);
	void (*downgrade_oplock)(struct TCP_Server_Info *server,
				 struct cifsInodeInfo *cinode, __u32 oplock,
				 unsigned int epoch, bool *purge_cache);
	/* process transaction2 response */
	bool (*check_trans2)(struct mid_q_entry *, struct TCP_Server_Info *,
			     char *, int);
	/* check if we need to negotiate */
	bool (*need_neg)(struct TCP_Server_Info *);
	/* negotiate to the server */
	int (*negotiate)(const unsigned int xid,
			 struct cifs_ses *ses,
			 struct TCP_Server_Info *server);
	/* set negotiated write size */
	unsigned int (*negotiate_wsize)(struct cifs_tcon *tcon, struct smb3_fs_context *ctx);
	/* set negotiated read size */
	unsigned int (*negotiate_rsize)(struct cifs_tcon *tcon, struct smb3_fs_context *ctx);
	/* setup smb sessionn */
	int (*sess_setup)(const unsigned int, struct cifs_ses *,
			  struct TCP_Server_Info *server,
			  const struct nls_table *);
	/* close smb session */
	int (*logoff)(const unsigned int, struct cifs_ses *);
	/* connect to a server share */
	int (*tree_connect)(const unsigned int, struct cifs_ses *, const char *,
			    struct cifs_tcon *, const struct nls_table *);
	/* close tree connection */
	int (*tree_disconnect)(const unsigned int, struct cifs_tcon *);
	/* get DFS referrals */
	int (*get_dfs_refer)(const unsigned int, struct cifs_ses *,
			     const char *, struct dfs_info3_param **,
			     unsigned int *, const struct nls_table *, int);
	/* informational QFS call */
	void (*qfs_tcon)(const unsigned int, struct cifs_tcon *,
			 struct cifs_sb_info *);
	/* query for server interfaces */
	int (*query_server_interfaces)(const unsigned int, struct cifs_tcon *,
				       bool);
	/* check if a path is accessible or not */
	int (*is_path_accessible)(const unsigned int, struct cifs_tcon *,
				  struct cifs_sb_info *, const char *);
	/* query path data from the server */
	int (*query_path_info)(const unsigned int xid,
			       struct cifs_tcon *tcon,
			       struct cifs_sb_info *cifs_sb,
			       const char *full_path,
			       struct cifs_open_info_data *data);
	/* query file data from the server */
	int (*query_file_info)(const unsigned int xid, struct cifs_tcon *tcon,
			       struct cifsFileInfo *cfile, struct cifs_open_info_data *data);
	/* query reparse point to determine which type of special file */
	int (*query_reparse_point)(const unsigned int xid,
				   struct cifs_tcon *tcon,
				   struct cifs_sb_info *cifs_sb,
				   const char *full_path,
				   u32 *tag, struct kvec *rsp,
				   int *rsp_buftype);
	/* get server index number */
	int (*get_srv_inum)(const unsigned int xid, struct cifs_tcon *tcon,
			    struct cifs_sb_info *cifs_sb, const char *full_path, u64 *uniqueid,
			    struct cifs_open_info_data *data);
	/* set size by path */
	int (*set_path_size)(const unsigned int, struct cifs_tcon *,
			     const char *, __u64, struct cifs_sb_info *, bool,
				 struct dentry *);
	/* set size by file handle */
	int (*set_file_size)(const unsigned int, struct cifs_tcon *,
			     struct cifsFileInfo *, __u64, bool);
	/* set attributes */
	int (*set_file_info)(struct inode *, const char *, FILE_BASIC_INFO *,
			     const unsigned int);
	int (*set_compression)(const unsigned int, struct cifs_tcon *,
			       struct cifsFileInfo *);
	/* check if we can send an echo or nor */
	bool (*can_echo)(struct TCP_Server_Info *);
	/* send echo request */
	int (*echo)(struct TCP_Server_Info *);
	/* create directory */
	int (*posix_mkdir)(const unsigned int xid, struct inode *inode,
			umode_t mode, struct cifs_tcon *tcon,
			const char *full_path,
			struct cifs_sb_info *cifs_sb);
	int (*mkdir)(const unsigned int xid, struct inode *inode, umode_t mode,
		     struct cifs_tcon *tcon, const char *name,
		     struct cifs_sb_info *sb);
	/* set info on created directory */
	void (*mkdir_setinfo)(struct inode *, const char *,
			      struct cifs_sb_info *, struct cifs_tcon *,
			      const unsigned int);
	/* remove directory */
	int (*rmdir)(const unsigned int, struct cifs_tcon *, const char *,
		     struct cifs_sb_info *);
	/* unlink file */
	int (*unlink)(const unsigned int, struct cifs_tcon *, const char *,
		      struct cifs_sb_info *, struct dentry *);
	/* open, rename and delete file */
	int (*rename_pending_delete)(const char *, struct dentry *,
				     const unsigned int);
	/* send rename request */
	int (*rename)(const unsigned int xid,
		      struct cifs_tcon *tcon,
		      struct dentry *source_dentry,
		      const char *from_name, const char *to_name,
		      struct cifs_sb_info *cifs_sb);
	/* send create hardlink request */
	int (*create_hardlink)(const unsigned int xid,
			       struct cifs_tcon *tcon,
			       struct dentry *source_dentry,
			       const char *from_name, const char *to_name,
			       struct cifs_sb_info *cifs_sb);
	/* query symlink target */
	int (*query_symlink)(const unsigned int xid,
			     struct cifs_tcon *tcon,
			     struct cifs_sb_info *cifs_sb,
			     const char *full_path,
			     char **target_path);
	/* open a file for non-posix mounts */
	int (*open)(const unsigned int xid, struct cifs_open_parms *oparms, __u32 *oplock,
		    void *buf);
	/* set fid protocol-specific info */
	void (*set_fid)(struct cifsFileInfo *, struct cifs_fid *, __u32);
	/* close a file */
	int (*close)(const unsigned int, struct cifs_tcon *,
		      struct cifs_fid *);
	/* close a file, returning file attributes and timestamps */
	int (*close_getattr)(const unsigned int xid, struct cifs_tcon *tcon,
		      struct cifsFileInfo *pfile_info);
	/* send a flush request to the server */
	int (*flush)(const unsigned int, struct cifs_tcon *, struct cifs_fid *);
	/* async read from the server */
	int (*async_readv)(struct cifs_io_subrequest *);
	/* async write to the server */
	void (*async_writev)(struct cifs_io_subrequest *);
	/* sync read from the server */
	int (*sync_read)(const unsigned int, struct cifs_fid *,
			 struct cifs_io_parms *, unsigned int *, char **,
			 int *);
	/* sync write to the server */
	int (*sync_write)(const unsigned int, struct cifs_fid *,
			  struct cifs_io_parms *, unsigned int *, struct kvec *,
			  unsigned long);
	/* open dir, start readdir */
	int (*query_dir_first)(const unsigned int, struct cifs_tcon *,
			       const char *, struct cifs_sb_info *,
			       struct cifs_fid *, __u16,
			       struct cifs_search_info *);
	/* continue readdir */
	int (*query_dir_next)(const unsigned int, struct cifs_tcon *,
			      struct cifs_fid *,
			      __u16, struct cifs_search_info *srch_inf);
	/* close dir */
	int (*close_dir)(const unsigned int, struct cifs_tcon *,
			 struct cifs_fid *);
	/* calculate a size of SMB message */
	unsigned int (*calc_smb_size)(void *buf);
	/* check for STATUS_PENDING and process the response if yes */
	bool (*is_status_pending)(char *buf, struct TCP_Server_Info *server);
	/* check for STATUS_NETWORK_SESSION_EXPIRED */
	bool (*is_session_expired)(char *);
	/* send oplock break response */
	int (*oplock_response)(struct cifs_tcon *tcon, __u64 persistent_fid, __u64 volatile_fid,
			__u16 net_fid, struct cifsInodeInfo *cifs_inode);
	/* query remote filesystem */
	int (*queryfs)(const unsigned int, struct cifs_tcon *,
		       const char *, struct cifs_sb_info *, struct kstatfs *);
	/* send mandatory brlock to the server */
	int (*mand_lock)(const unsigned int, struct cifsFileInfo *, __u64,
			 __u64, __u32, int, int, bool);
	/* unlock range of mandatory locks */
	int (*mand_unlock_range)(struct cifsFileInfo *, struct file_lock *,
				 const unsigned int);
	/* push brlocks from the cache to the server */
	int (*push_mand_locks)(struct cifsFileInfo *);
	/* get lease key of the inode */
	void (*get_lease_key)(struct inode *, struct cifs_fid *);
	/* set lease key of the inode */
	void (*set_lease_key)(struct inode *, struct cifs_fid *);
	/* generate new lease key */
	void (*new_lease_key)(struct cifs_fid *);
	int (*generate_signingkey)(struct cifs_ses *ses,
				   struct TCP_Server_Info *server);
	int (*calc_signature)(struct smb_rqst *, struct TCP_Server_Info *,
				bool allocate_crypto);
	int (*set_integrity)(const unsigned int, struct cifs_tcon *tcon,
			     struct cifsFileInfo *src_file);
	int (*enum_snapshots)(const unsigned int xid, struct cifs_tcon *tcon,
			     struct cifsFileInfo *src_file, void __user *);
	int (*notify)(const unsigned int xid, struct file *pfile,
			     void __user *pbuf, bool return_changes);
	int (*query_mf_symlink)(unsigned int, struct cifs_tcon *,
				struct cifs_sb_info *, const unsigned char *,
				char *, unsigned int *);
	int (*create_mf_symlink)(unsigned int, struct cifs_tcon *,
				 struct cifs_sb_info *, const unsigned char *,
				 char *, unsigned int *);
	/* if we can do cache read operations */
	bool (*is_read_op)(__u32);
	/* set oplock level for the inode */
	void (*set_oplock_level)(struct cifsInodeInfo *, __u32, unsigned int,
				 bool *);
	/* create lease context buffer for CREATE request */
	char * (*create_lease_buf)(u8 *lease_key, u8 oplock);
	/* parse lease context buffer and return oplock/epoch info */
	__u8 (*parse_lease_buf)(void *buf, unsigned int *epoch, char *lkey);
	ssize_t (*copychunk_range)(const unsigned int,
			struct cifsFileInfo *src_file,
			struct cifsFileInfo *target_file,
			u64 src_off, u64 len, u64 dest_off);
	int (*duplicate_extents)(const unsigned int, struct cifsFileInfo *src,
			struct cifsFileInfo *target_file, u64 src_off, u64 len,
			u64 dest_off);
	int (*validate_negotiate)(const unsigned int, struct cifs_tcon *);
	ssize_t (*query_all_EAs)(const unsigned int, struct cifs_tcon *,
			const unsigned char *, const unsigned char *, char *,
			size_t, struct cifs_sb_info *);
	int (*set_EA)(const unsigned int, struct cifs_tcon *, const char *,
			const char *, const void *, const __u16,
			const struct nls_table *, struct cifs_sb_info *);
	struct smb_ntsd * (*get_acl)(struct cifs_sb_info *cifssb, struct inode *ino,
			const char *patch, u32 *plen, u32 info);
	struct smb_ntsd * (*get_acl_by_fid)(struct cifs_sb_info *cifssmb,
			const struct cifs_fid *pfid, u32 *plen, u32 info);
	int (*set_acl)(struct smb_ntsd *pntsd, __u32 len, struct inode *ino, const char *path,
			int flag);
	/* writepages retry size */
	unsigned int (*wp_retry_size)(struct inode *);
	/* get mtu credits */
	int (*wait_mtu_credits)(struct TCP_Server_Info *, size_t,
				size_t *, struct cifs_credits *);
	/* adjust previously taken mtu credits to request size */
	int (*adjust_credits)(struct TCP_Server_Info *server,
			      struct cifs_io_subrequest *subreq,
			      unsigned int /*enum smb3_rw_credits_trace*/ trace);
	/* check if we need to issue closedir */
	bool (*dir_needs_close)(struct cifsFileInfo *);
	long (*fallocate)(struct file *, struct cifs_tcon *, int, loff_t,
			  loff_t);
	/* init transform (compress/encrypt) request */
	int (*init_transform_rq)(struct TCP_Server_Info *, int num_rqst,
				 struct smb_rqst *, struct smb_rqst *);
	int (*is_transform_hdr)(void *buf);
	int (*receive_transform)(struct TCP_Server_Info *,
				 struct mid_q_entry **, char **, int *);
	enum securityEnum (*select_sectype)(struct TCP_Server_Info *,
			    enum securityEnum);
	int (*next_header)(struct TCP_Server_Info *server, char *buf,
			   unsigned int *noff);
	/* ioctl passthrough for query_info */
	int (*ioctl_query_info)(const unsigned int xid,
				struct cifs_tcon *tcon,
				struct cifs_sb_info *cifs_sb,
				__le16 *path, int is_dir,
				unsigned long p);
	/* make unix special files (block, char, fifo, socket) */
	int (*make_node)(unsigned int xid,
			 struct inode *inode,
			 struct dentry *dentry,
			 struct cifs_tcon *tcon,
			 const char *full_path,
			 umode_t mode,
			 dev_t device_number);
	/* version specific fiemap implementation */
	int (*fiemap)(struct cifs_tcon *tcon, struct cifsFileInfo *,
		      struct fiemap_extent_info *, u64, u64);
	/* version specific llseek implementation */
	loff_t (*llseek)(struct file *, struct cifs_tcon *, loff_t, int);
	/* Check for STATUS_IO_TIMEOUT */
	bool (*is_status_io_timeout)(char *buf);
	/* Check for STATUS_NETWORK_NAME_DELETED */
	bool (*is_network_name_deleted)(char *buf, struct TCP_Server_Info *srv);
	int (*parse_reparse_point)(struct cifs_sb_info *cifs_sb,
				   struct kvec *rsp_iov,
				   struct cifs_open_info_data *data);
	int (*create_reparse_symlink)(const unsigned int xid,
				      struct inode *inode,
				      struct dentry *dentry,
				      struct cifs_tcon *tcon,
				      const char *full_path,
				      const char *symname);
};

struct smb_version_values {
	char		*version_string;
	__u16		protocol_id;
	__u32		req_capabilities;
	__u32		large_lock_type;
	__u32		exclusive_lock_type;
	__u32		shared_lock_type;
	__u32		unlock_lock_type;
	size_t		header_preamble_size;
	size_t		header_size;
	size_t		max_header_size;
	size_t		read_rsp_size;
	__le16		lock_cmd;
	unsigned int	cap_unix;
	unsigned int	cap_nt_find;
	unsigned int	cap_large_files;
	__u16		signing_enabled;
	__u16		signing_required;
	size_t		create_lease_size;
};

#define HEADER_SIZE(server) (server->vals->header_size)
#define MAX_HEADER_SIZE(server) (server->vals->max_header_size)
#define HEADER_PREAMBLE_SIZE(server) (server->vals->header_preamble_size)
#define MID_HEADER_SIZE(server) (HEADER_SIZE(server) - 1 - HEADER_PREAMBLE_SIZE(server))

/**
 * CIFS superblock mount flags (mnt_cifs_flags) to consider when
 * trying to reuse existing superblock for a new mount
 */
#define CIFS_MOUNT_MASK (CIFS_MOUNT_NO_PERM | CIFS_MOUNT_SET_UID | \
			 CIFS_MOUNT_SERVER_INUM | CIFS_MOUNT_DIRECT_IO | \
			 CIFS_MOUNT_NO_XATTR | CIFS_MOUNT_MAP_SPECIAL_CHR | \
			 CIFS_MOUNT_MAP_SFM_CHR | \
			 CIFS_MOUNT_UNX_EMUL | CIFS_MOUNT_NO_BRL | \
			 CIFS_MOUNT_CIFS_ACL | CIFS_MOUNT_OVERR_UID | \
			 CIFS_MOUNT_OVERR_GID | CIFS_MOUNT_DYNPERM | \
			 CIFS_MOUNT_NOPOSIXBRL | CIFS_MOUNT_NOSSYNC | \
			 CIFS_MOUNT_FSCACHE | CIFS_MOUNT_MF_SYMLINKS | \
			 CIFS_MOUNT_MULTIUSER | CIFS_MOUNT_STRICT_IO | \
			 CIFS_MOUNT_CIFS_BACKUPUID | CIFS_MOUNT_CIFS_BACKUPGID | \
			 CIFS_MOUNT_UID_FROM_ACL | CIFS_MOUNT_NO_HANDLE_CACHE | \
			 CIFS_MOUNT_NO_DFS | CIFS_MOUNT_MODE_FROM_SID | \
			 CIFS_MOUNT_RO_CACHE | CIFS_MOUNT_RW_CACHE)

/**
 * Generic VFS superblock mount flags (s_flags) to consider when
 * trying to reuse existing superblock for a new mount
 */
#define CIFS_MS_MASK (SB_RDONLY | SB_MANDLOCK | SB_NOEXEC | SB_NOSUID | \
		      SB_NODEV | SB_SYNCHRONOUS)

struct cifs_mnt_data {
	struct cifs_sb_info *cifs_sb;
	struct smb3_fs_context *ctx;
	int flags;
};

static inline unsigned int
get_rfc1002_length(void *buf)
{
	return be32_to_cpu(*((__be32 *)buf)) & 0xffffff;
}

static inline void
inc_rfc1001_len(void *buf, int count)
{
	be32_add_cpu((__be32 *)buf, count);
}

struct TCP_Server_Info {
	struct list_head tcp_ses_list;
	struct list_head smb_ses_list;
	spinlock_t srv_lock;  /* protect anything here that is not protected */
	__u64 conn_id; /* connection identifier (useful for debugging) */
	int srv_count; /* reference counter */
	/* 15 character server name + 0x20 16th byte indicating type = srv */
	char server_RFC1001_name[RFC1001_NAME_LEN_WITH_NULL];
	struct smb_version_operations	*ops;
	struct smb_version_values	*vals;
	/* updates to tcpStatus protected by cifs_tcp_ses_lock */
	enum statusEnum tcpStatus; /* what we think the status is */
	char *hostname; /* hostname portion of UNC string */
	struct socket *ssocket;
	struct sockaddr_storage dstaddr;
	struct sockaddr_storage srcaddr; /* locally bind to this IP */
#ifdef CONFIG_NET_NS
	struct net *net;
#endif
	wait_queue_head_t response_q;
	wait_queue_head_t request_q; /* if more than maxmpx to srvr must block*/
	spinlock_t mid_lock;  /* protect mid queue and it's entries */
	struct list_head pending_mid_q;
	bool noblocksnd;		/* use blocking sendmsg */
	bool noautotune;		/* do not autotune send buf sizes */
	bool nosharesock;
	bool tcp_nodelay;
	bool terminate;
	unsigned int credits;  /* send no more requests at once */
	unsigned int max_credits; /* can override large 32000 default at mnt */
	unsigned int in_flight;  /* number of requests on the wire to server */
	unsigned int max_in_flight; /* max number of requests that were on wire */
	spinlock_t req_lock;  /* protect the two values above */
	struct mutex _srv_mutex;
	unsigned int nofs_flag;
	struct task_struct *tsk;
	char server_GUID[16];
	__u16 sec_mode;
	bool sign; /* is signing enabled on this connection? */
	bool ignore_signature:1; /* skip validation of signatures in SMB2/3 rsp */
	bool session_estab; /* mark when very first sess is established */
	int echo_credits;  /* echo reserved slots */
	int oplock_credits;  /* oplock break reserved slots */
	bool echoes:1; /* enable echoes */
	__u8 client_guid[SMB2_CLIENT_GUID_SIZE]; /* Client GUID */
	u16 dialect; /* dialect index that server chose */
	bool oplocks:1; /* enable oplocks */
	unsigned int maxReq;	/* Clients should submit no more */
	/* than maxReq distinct unanswered SMBs to the server when using  */
	/* multiplexed reads or writes (for SMB1/CIFS only, not SMB2/SMB3) */
	unsigned int maxBuf;	/* maxBuf specifies the maximum */
	/* message size the server can send or receive for non-raw SMBs */
	/* maxBuf is returned by SMB NegotiateProtocol so maxBuf is only 0 */
	/* when socket is setup (and during reconnect) before NegProt sent */
	unsigned int max_rw;	/* maxRw specifies the maximum */
	/* message size the server can send or receive for */
	/* SMB_COM_WRITE_RAW or SMB_COM_READ_RAW. */
	unsigned int capabilities; /* selective disabling of caps by smb sess */
	int timeAdj;  /* Adjust for difference in server time zone in sec */
	__u64 CurrentMid;         /* multiplex id - rotating counter, protected by GlobalMid_Lock */
	char cryptkey[CIFS_CRYPTO_KEY_SIZE]; /* used by ntlm, ntlmv2 etc */
	/* 16th byte of RFC1001 workstation name is always null */
	char workstation_RFC1001_name[RFC1001_NAME_LEN_WITH_NULL];
	__u32 sequence_number; /* for signing, protected by srv_mutex */
	__u32 reconnect_instance; /* incremented on each reconnect */
	struct session_key session_key;
	unsigned long lstrp; /* when we got last response from this server */
	struct cifs_secmech secmech; /* crypto sec mech functs, descriptors */
#define	CIFS_NEGFLAVOR_UNENCAP	1	/* wct == 17, but no ext_sec */
#define	CIFS_NEGFLAVOR_EXTENDED	2	/* wct == 17, ext_sec bit set */
	char	negflavor;	/* NEGOTIATE response flavor */
	/* extended security flavors that server supports */
	bool	sec_ntlmssp;		/* supports NTLMSSP */
	bool	sec_kerberosu2u;	/* supports U2U Kerberos */
	bool	sec_kerberos;		/* supports plain Kerberos */
	bool	sec_mskerberos;		/* supports legacy MS Kerberos */
	bool	large_buf;		/* is current buffer large? */
	/* use SMBD connection instead of socket */
	bool	rdma;
	/* point to the SMBD connection if RDMA is used instead of socket */
	struct smbd_connection *smbd_conn;
	struct delayed_work	echo; /* echo ping workqueue job */
	char	*smallbuf;	/* pointer to current "small" buffer */
	char	*bigbuf;	/* pointer to current "big" buffer */
	/* Total size of this PDU. Only valid from cifs_demultiplex_thread */
	unsigned int pdu_size;
	unsigned int total_read; /* total amount of data read in this pass */
	atomic_t in_send; /* requests trying to send */
	atomic_t num_waiters;   /* blocked waiting to get in sendrecv */
#ifdef CONFIG_CIFS_STATS2
	atomic_t num_cmds[NUMBER_OF_SMB2_COMMANDS]; /* total requests by cmd */
	atomic_t smb2slowcmd[NUMBER_OF_SMB2_COMMANDS]; /* count resps > 1 sec */
	__u64 time_per_cmd[NUMBER_OF_SMB2_COMMANDS]; /* total time per cmd */
	__u32 slowest_cmd[NUMBER_OF_SMB2_COMMANDS];
	__u32 fastest_cmd[NUMBER_OF_SMB2_COMMANDS];
#endif /* STATS2 */
	unsigned int	max_read;
	unsigned int	max_write;
	unsigned int	min_offload;
	unsigned int	retrans;
	struct {
		bool requested; /* "compress" mount option set*/
		bool enabled; /* actually negotiated with server */
		__le16 alg; /* preferred alg negotiated with server */
	} compression;
	__u16	signing_algorithm;
	__le16	cipher_type;
	 /* save initial negprot hash */
	__u8	preauth_sha_hash[SMB2_PREAUTH_HASH_SIZE];
	bool	signing_negotiated; /* true if valid signing context rcvd from server */
	bool	posix_ext_supported;
	struct delayed_work reconnect; /* reconnect workqueue job */
	struct mutex reconnect_mutex; /* prevent simultaneous reconnects */
	unsigned long echo_interval;

	/*
	 * Number of targets available for reconnect. The more targets
	 * the more tasks have to wait to let the demultiplex thread
	 * reconnect.
	 */
	int nr_targets;
	bool noblockcnt; /* use non-blocking connect() */

	/*
	 * If this is a session channel,
	 * primary_server holds the ref-counted
	 * pointer to primary channel connection for the session.
	 */
#define SERVER_IS_CHAN(server)	(!!(server)->primary_server)
	struct TCP_Server_Info *primary_server;
	__u16 channel_sequence_num;  /* incremented on primary channel on each chan reconnect */

#ifdef CONFIG_CIFS_SWN_UPCALL
	bool use_swn_dstaddr;
	struct sockaddr_storage swn_dstaddr;
#endif
	struct mutex refpath_lock; /* protects leaf_fullpath */
	/*
	 * leaf_fullpath: Canonical DFS referral path related to this
	 *                connection.
	 *                It is used in DFS cache refresher, reconnect and may
	 *                change due to nested DFS links.
	 *
	 * Protected by @refpath_lock and @srv_lock.  The @refpath_lock is
	 * mostly used for not requiring a copy of @leaf_fullpath when getting
	 * cached or new DFS referrals (which might also sleep during I/O).
	 * While @srv_lock is held for making string and NULL comparisons against
	 * both fields as in mount(2) and cache refresh.
	 *
	 * format: \\HOST\SHARE[\OPTIONAL PATH]
	 */
	char *leaf_fullpath;
	bool dfs_conn:1;
};

static inline bool is_smb1(struct TCP_Server_Info *server)
{
	return HEADER_PREAMBLE_SIZE(server) != 0;
}

static inline void cifs_server_lock(struct TCP_Server_Info *server)
{
	unsigned int nofs_flag = memalloc_nofs_save();

	mutex_lock(&server->_srv_mutex);
	server->nofs_flag = nofs_flag;
}

static inline void cifs_server_unlock(struct TCP_Server_Info *server)
{
	unsigned int nofs_flag = server->nofs_flag;

	mutex_unlock(&server->_srv_mutex);
	memalloc_nofs_restore(nofs_flag);
}

struct cifs_credits {
	unsigned int value;
	unsigned int instance;
	unsigned int in_flight_check;
	unsigned int rreq_debug_id;
	unsigned int rreq_debug_index;
};

static inline unsigned int
in_flight(struct TCP_Server_Info *server)
{
	unsigned int num;

	spin_lock(&server->req_lock);
	num = server->in_flight;
	spin_unlock(&server->req_lock);
	return num;
}

static inline bool
has_credits(struct TCP_Server_Info *server, int *credits, int num_credits)
{
	int num;

	spin_lock(&server->req_lock);
	num = *credits;
	spin_unlock(&server->req_lock);
	return num >= num_credits;
}

static inline void
add_credits(struct TCP_Server_Info *server, struct cifs_credits *credits,
	    const int optype)
{
	server->ops->add_credits(server, credits, optype);
}

static inline void
add_credits_and_wake_if(struct TCP_Server_Info *server,
			struct cifs_credits *credits, const int optype)
{
	if (credits->value) {
		server->ops->add_credits(server, credits, optype);
		wake_up(&server->request_q);
		credits->value = 0;
	}
}

static inline void
set_credits(struct TCP_Server_Info *server, const int val)
{
	server->ops->set_credits(server, val);
}

static inline int
adjust_credits(struct TCP_Server_Info *server, struct cifs_io_subrequest *subreq,
	       unsigned int /* enum smb3_rw_credits_trace */ trace)
{
	return server->ops->adjust_credits ?
		server->ops->adjust_credits(server, subreq, trace) : 0;
}

static inline __le64
get_next_mid64(struct TCP_Server_Info *server)
{
	return cpu_to_le64(server->ops->get_next_mid(server));
}

static inline __le16
get_next_mid(struct TCP_Server_Info *server)
{
	__u16 mid = server->ops->get_next_mid(server);
	/*
	 * The value in the SMB header should be little endian for easy
	 * on-the-wire decoding.
	 */
	return cpu_to_le16(mid);
}

static inline void
revert_current_mid(struct TCP_Server_Info *server, const unsigned int val)
{
	if (server->ops->revert_current_mid)
		server->ops->revert_current_mid(server, val);
}

static inline void
revert_current_mid_from_hdr(struct TCP_Server_Info *server,
			    const struct smb2_hdr *shdr)
{
	unsigned int num = le16_to_cpu(shdr->CreditCharge);

	return revert_current_mid(server, num > 0 ? num : 1);
}

static inline __u16
get_mid(const struct smb_hdr *smb)
{
	return le16_to_cpu(smb->Mid);
}

static inline bool
compare_mid(__u16 mid, const struct smb_hdr *smb)
{
	return mid == le16_to_cpu(smb->Mid);
}

/*
 * When the server supports very large reads and writes via POSIX extensions,
 * we can allow up to 2^24-1, minus the size of a READ/WRITE_AND_X header, not
 * including the RFC1001 length.
 *
 * Note that this might make for "interesting" allocation problems during
 * writeback however as we have to allocate an array of pointers for the
 * pages. A 16M write means ~32kb page array with PAGE_SIZE == 4096.
 *
 * For reads, there is a similar problem as we need to allocate an array
 * of kvecs to handle the receive, though that should only need to be done
 * once.
 */
#define CIFS_MAX_WSIZE ((1<<24) - 1 - sizeof(WRITE_REQ) + 4)
#define CIFS_MAX_RSIZE ((1<<24) - sizeof(READ_RSP) + 4)

/*
 * When the server doesn't allow large posix writes, only allow a rsize/wsize
 * of 2^17-1 minus the size of the call header. That allows for a read or
 * write up to the maximum size described by RFC1002.
 */
#define CIFS_MAX_RFC1002_WSIZE ((1<<17) - 1 - sizeof(WRITE_REQ) + 4)
#define CIFS_MAX_RFC1002_RSIZE ((1<<17) - 1 - sizeof(READ_RSP) + 4)

#define CIFS_DEFAULT_IOSIZE (1024 * 1024)

/*
 * Windows only supports a max of 60kb reads and 65535 byte writes. Default to
 * those values when posix extensions aren't in force. In actuality here, we
 * use 65536 to allow for a write that is a multiple of 4k. Most servers seem
 * to be ok with the extra byte even though Windows doesn't send writes that
 * are that large.
 *
 * Citation:
 *
 * https://blogs.msdn.com/b/openspecification/archive/2009/04/10/smb-maximum-transmit-buffer-size-and-performance-tuning.aspx
 */
#define CIFS_DEFAULT_NON_POSIX_RSIZE (60 * 1024)
#define CIFS_DEFAULT_NON_POSIX_WSIZE (65536)

/*
 * Macros to allow the TCP_Server_Info->net field and related code to drop out
 * when CONFIG_NET_NS isn't set.
 */

#ifdef CONFIG_NET_NS

static inline struct net *cifs_net_ns(struct TCP_Server_Info *srv)
{
	return srv->net;
}

static inline void cifs_set_net_ns(struct TCP_Server_Info *srv, struct net *net)
{
	srv->net = net;
}

#else

static inline struct net *cifs_net_ns(struct TCP_Server_Info *srv)
{
	return &init_net;
}

static inline void cifs_set_net_ns(struct TCP_Server_Info *srv, struct net *net)
{
}

#endif

struct cifs_server_iface {
	struct list_head iface_head;
	struct kref refcount;
	size_t speed;
	size_t weight_fulfilled;
	unsigned int num_channels;
	unsigned int rdma_capable : 1;
	unsigned int rss_capable : 1;
	unsigned int is_active : 1; /* unset if non existent */
	struct sockaddr_storage sockaddr;
};

/* release iface when last ref is dropped */
static inline void
release_iface(struct kref *ref)
{
	struct cifs_server_iface *iface = container_of(ref,
						       struct cifs_server_iface,
						       refcount);
	kfree(iface);
}

struct cifs_chan {
	unsigned int in_reconnect : 1; /* if session setup in progress for this channel */
	struct TCP_Server_Info *server;
	struct cifs_server_iface *iface; /* interface in use */
	__u8 signkey[SMB3_SIGN_KEY_SIZE];
};

#define CIFS_SES_FLAG_SCALE_CHANNELS (0x1)

/*
 * Session structure.  One of these for each uid session with a particular host
 */
struct cifs_ses {
	struct list_head smb_ses_list;
	struct list_head rlist; /* reconnect list */
	struct list_head tcon_list;
	struct list_head dlist; /* dfs list */
	struct cifs_tcon *tcon_ipc;
	spinlock_t ses_lock;  /* protect anything here that is not protected */
	struct mutex session_mutex;
	struct TCP_Server_Info *server;	/* pointer to server info */
	int ses_count;		/* reference counter */
	enum ses_status_enum ses_status;  /* updates protected by cifs_tcp_ses_lock */
	unsigned int overrideSecFlg; /* if non-zero override global sec flags */
	char *serverOS;		/* name of operating system underlying server */
	char *serverNOS;	/* name of network operating system of server */
	char *serverDomain;	/* security realm of server */
	__u64 Suid;		/* remote smb uid  */
	kuid_t linux_uid;	/* overriding owner of files on the mount */
	kuid_t cred_uid;	/* owner of credentials */
	unsigned int capabilities;
	char ip_addr[INET6_ADDRSTRLEN + 1]; /* Max ipv6 (or v4) addr string len */
	char *user_name;	/* must not be null except during init of sess
				   and after mount option parsing we fill it */
	char *domainName;
	char *password;
	char *password2; /* When key rotation used, new password may be set before it expires */
	char workstation_name[CIFS_MAX_WORKSTATION_LEN];
	struct session_key auth_key;
	struct ntlmssp_auth *ntlmssp; /* ciphertext, flags, server challenge */
	enum securityEnum sectype; /* what security flavor was specified? */
	bool sign;		/* is signing required? */
	bool domainAuto:1;
	bool expired_pwd;  /* track if access denied or expired pwd so can know if need to update */
	unsigned int flags;
	__u16 session_flags;
	__u8 smb3signingkey[SMB3_SIGN_KEY_SIZE];
	__u8 smb3encryptionkey[SMB3_ENC_DEC_KEY_SIZE];
	__u8 smb3decryptionkey[SMB3_ENC_DEC_KEY_SIZE];
	__u8 preauth_sha_hash[SMB2_PREAUTH_HASH_SIZE];

	/*
	 * Network interfaces available on the server this session is
	 * connected to.
	 *
	 * Other channels can be opened by connecting and binding this
	 * session to interfaces from this list.
	 *
	 * iface_lock should be taken when accessing any of these fields
	 */
	spinlock_t iface_lock;
	/* ========= begin: protected by iface_lock ======== */
	struct list_head iface_list;
	size_t iface_count;
	unsigned long iface_last_update; /* jiffies */
	/* ========= end: protected by iface_lock ======== */

	spinlock_t chan_lock;
	/* ========= begin: protected by chan_lock ======== */
#define CIFS_MAX_CHANNELS 16
#define CIFS_INVAL_CHAN_INDEX (-1)
#define CIFS_ALL_CHANNELS_SET(ses)	\
	((1UL << (ses)->chan_count) - 1)
#define CIFS_ALL_CHANS_GOOD(ses)		\
	(!(ses)->chans_need_reconnect)
#define CIFS_ALL_CHANS_NEED_RECONNECT(ses)	\
	((ses)->chans_need_reconnect == CIFS_ALL_CHANNELS_SET(ses))
#define CIFS_SET_ALL_CHANS_NEED_RECONNECT(ses)	\
	((ses)->chans_need_reconnect = CIFS_ALL_CHANNELS_SET(ses))
#define CIFS_CHAN_NEEDS_RECONNECT(ses, index)	\
	test_bit((index), &(ses)->chans_need_reconnect)
#define CIFS_CHAN_IN_RECONNECT(ses, index)	\
	((ses)->chans[(index)].in_reconnect)

	struct cifs_chan chans[CIFS_MAX_CHANNELS];
	size_t chan_count;
	size_t chan_max;
	atomic_t chan_seq; /* round robin state */

	/*
	 * chans_need_reconnect is a bitmap indicating which of the channels
	 * under this smb session needs to be reconnected.
	 * If not multichannel session, only one bit will be used.
	 *
	 * We will ask for sess and tcon reconnection only if all the
	 * channels are marked for needing reconnection. This will
	 * enable the sessions on top to continue to live till any
	 * of the channels below are active.
	 */
	unsigned long chans_need_reconnect;
	/* ========= end: protected by chan_lock ======== */
	struct cifs_ses *dfs_root_ses;
	struct nls_table *local_nls;
};

static inline bool
cap_unix(struct cifs_ses *ses)
{
	return ses->server->vals->cap_unix & ses->capabilities;
}

/*
 * common struct for holding inode info when searching for or updating an
 * inode with new info
 */

#define CIFS_FATTR_JUNCTION		0x1
#define CIFS_FATTR_DELETE_PENDING	0x2
#define CIFS_FATTR_NEED_REVAL		0x4
#define CIFS_FATTR_INO_COLLISION	0x8
#define CIFS_FATTR_UNKNOWN_NLINK	0x10
#define CIFS_FATTR_FAKE_ROOT_INO	0x20

struct cifs_fattr {
	u32		cf_flags;
	u32		cf_cifsattrs;
	u64		cf_uniqueid;
	u64		cf_eof;
	u64		cf_bytes;
	u64		cf_createtime;
	kuid_t		cf_uid;
	kgid_t		cf_gid;
	umode_t		cf_mode;
	dev_t		cf_rdev;
	unsigned int	cf_nlink;
	unsigned int	cf_dtype;
	struct timespec64 cf_atime;
	struct timespec64 cf_mtime;
	struct timespec64 cf_ctime;
	u32             cf_cifstag;
	char            *cf_symlink_target;
};

/*
 * there is one of these for each connection to a resource on a particular
 * session
 */
struct cifs_tcon {
	struct list_head tcon_list;
	int debug_id;		/* Debugging for tracing */
	int tc_count;
	struct list_head rlist; /* reconnect list */
	spinlock_t tc_lock;  /* protect anything here that is not protected */
	atomic_t num_local_opens;  /* num of all opens including disconnected */
	atomic_t num_remote_opens; /* num of all network opens on server */
	struct list_head openFileList;
	spinlock_t open_file_lock; /* protects list above */
	struct cifs_ses *ses;	/* pointer to session associated with */
	char tree_name[MAX_TREE_SIZE + 1]; /* UNC name of resource in ASCII */
	char *nativeFileSystem;
	char *password;		/* for share-level security */
	__u32 tid;		/* The 4 byte tree id */
	__u16 Flags;		/* optional support bits */
	enum tid_status_enum status;
	atomic_t num_smbs_sent;
	union {
		struct {
			atomic_t num_writes;
			atomic_t num_reads;
			atomic_t num_flushes;
			atomic_t num_oplock_brks;
			atomic_t num_opens;
			atomic_t num_closes;
			atomic_t num_deletes;
			atomic_t num_mkdirs;
			atomic_t num_posixopens;
			atomic_t num_posixmkdirs;
			atomic_t num_rmdirs;
			atomic_t num_renames;
			atomic_t num_t2renames;
			atomic_t num_ffirst;
			atomic_t num_fnext;
			atomic_t num_fclose;
			atomic_t num_hardlinks;
			atomic_t num_symlinks;
			atomic_t num_locks;
			atomic_t num_acl_get;
			atomic_t num_acl_set;
		} cifs_stats;
		struct {
			atomic_t smb2_com_sent[NUMBER_OF_SMB2_COMMANDS];
			atomic_t smb2_com_failed[NUMBER_OF_SMB2_COMMANDS];
		} smb2_stats;
	} stats;
	__u64    bytes_read;
	__u64    bytes_written;
	spinlock_t stat_lock;  /* protects the two fields above */
	time64_t stats_from_time;
	FILE_SYSTEM_DEVICE_INFO fsDevInfo;
	FILE_SYSTEM_ATTRIBUTE_INFO fsAttrInfo; /* ok if fs name truncated */
	FILE_SYSTEM_UNIX_INFO fsUnixInfo;
	bool ipc:1;   /* set if connection to IPC$ share (always also pipe) */
	bool pipe:1;  /* set if connection to pipe share */
	bool print:1; /* set if connection to printer share */
	bool retry:1;
	bool nocase:1;
	bool nohandlecache:1; /* if strange server resource prob can turn off */
	bool nodelete:1;
	bool seal:1;      /* transport encryption for this mounted share */
	bool unix_ext:1;  /* if false disable Linux extensions to CIFS protocol
				for this mount even if server would support */
	bool posix_extensions; /* if true SMB3.11 posix extensions enabled */
	bool local_lease:1; /* check leases (only) on local system not remote */
	bool broken_posix_open; /* e.g. Samba server versions < 3.3.2, 3.2.9 */
	bool broken_sparse_sup; /* if server or share does not support sparse */
	bool need_reconnect:1; /* connection reset, tid now invalid */
	bool need_reopen_files:1; /* need to reopen tcon file handles */
	bool use_resilient:1; /* use resilient instead of durable handles */
	bool use_persistent:1; /* use persistent instead of durable handles */
	bool no_lease:1;    /* Do not request leases on files or directories */
	bool use_witness:1; /* use witness protocol */
	__le32 capabilities;
	__u32 share_flags;
	__u32 maximal_access;
	__u32 vol_serial_number;
	__le64 vol_create_time;
	__u64 snapshot_time; /* for timewarp tokens - timestamp of snapshot */
	__u32 handle_timeout; /* persistent and durable handle timeout in ms */
	__u32 ss_flags;		/* sector size flags */
	__u32 perf_sector_size; /* best sector size for perf */
	__u32 max_chunks;
	__u32 max_bytes_chunk;
	__u32 max_bytes_copy;
	__u32 max_cached_dirs;
#ifdef CONFIG_CIFS_FSCACHE
	u64 resource_id;		/* server resource id */
	bool fscache_acquired;		/* T if we've tried acquiring a cookie */
	struct fscache_volume *fscache;	/* cookie for share */
	struct mutex fscache_lock;	/* Prevent regetting a cookie */
#endif
	struct list_head pending_opens;	/* list of incomplete opens */
	struct cached_fids *cfids;
	/* BB add field for back pointer to sb struct(s)? */
#ifdef CONFIG_CIFS_DFS_UPCALL
	struct delayed_work dfs_cache_work;
	struct list_head dfs_ses_list;
#endif
	struct delayed_work	query_interfaces; /* query interfaces workqueue job */
	char *origin_fullpath; /* canonical copy of smb3_fs_context::source */
};

/*
 * This is a refcounted and timestamped container for a tcon pointer. The
 * container holds a tcon reference. It is considered safe to free one of
 * these when the tl_count goes to 0. The tl_time is the time of the last
 * "get" on the container.
 */
struct tcon_link {
	struct rb_node		tl_rbnode;
	kuid_t			tl_uid;
	unsigned long		tl_flags;
#define TCON_LINK_MASTER	0
#define TCON_LINK_PENDING	1
#define TCON_LINK_IN_TREE	2
	unsigned long		tl_time;
	atomic_t		tl_count;
	struct cifs_tcon	*tl_tcon;
};

extern struct tcon_link *cifs_sb_tlink(struct cifs_sb_info *cifs_sb);
extern void smb3_free_compound_rqst(int num_rqst, struct smb_rqst *rqst);

static inline struct cifs_tcon *
tlink_tcon(struct tcon_link *tlink)
{
	return tlink->tl_tcon;
}

static inline struct tcon_link *
cifs_sb_master_tlink(struct cifs_sb_info *cifs_sb)
{
	return cifs_sb->master_tlink;
}

extern void cifs_put_tlink(struct tcon_link *tlink);

static inline struct tcon_link *
cifs_get_tlink(struct tcon_link *tlink)
{
	if (tlink && !IS_ERR(tlink))
		atomic_inc(&tlink->tl_count);
	return tlink;
}

/* This function is always expected to succeed */
extern struct cifs_tcon *cifs_sb_master_tcon(struct cifs_sb_info *cifs_sb);

#define CIFS_OPLOCK_NO_CHANGE 0xfe

struct cifs_pending_open {
	struct list_head olist;
	struct tcon_link *tlink;
	__u8 lease_key[16];
	__u32 oplock;
};

struct cifs_deferred_close {
	struct list_head dlist;
	struct tcon_link *tlink;
	__u16  netfid;
	__u64  persistent_fid;
	__u64  volatile_fid;
};

/*
 * This info hangs off the cifsFileInfo structure, pointed to by llist.
 * This is used to track byte stream locks on the file
 */
struct cifsLockInfo {
	struct list_head llist;	/* pointer to next cifsLockInfo */
	struct list_head blist; /* pointer to locks blocked on this */
	wait_queue_head_t block_q;
	__u64 offset;
	__u64 length;
	__u32 pid;
	__u16 type;
	__u16 flags;
};

/*
 * One of these for each open instance of a file
 */
struct cifs_search_info {
	loff_t index_of_last_entry;
	__u16 entries_in_buffer;
	__u16 info_level;
	__u32 resume_key;
	char *ntwrk_buf_start;
	char *srch_entries_start;
	char *last_entry;
	const char *presume_name;
	unsigned int resume_name_len;
	bool endOfSearch:1;
	bool emptyDir:1;
	bool unicode:1;
	bool smallBuf:1; /* so we know which buf_release function to call */
};

#define ACL_NO_MODE	((umode_t)(-1))
struct cifs_open_parms {
	struct cifs_tcon *tcon;
	struct cifs_sb_info *cifs_sb;
	int disposition;
	int desired_access;
	int create_options;
	const char *path;
	struct cifs_fid *fid;
	umode_t mode;
	bool reconnect:1;
	bool replay:1; /* indicates that this open is for a replay */
	struct kvec *ea_cctx;
};

struct cifs_fid {
	__u16 netfid;
	__u64 persistent_fid;	/* persist file id for smb2 */
	__u64 volatile_fid;	/* volatile file id for smb2 */
	__u8 lease_key[SMB2_LEASE_KEY_SIZE];	/* lease key for smb2 */
	__u8 create_guid[16];
	__u32 access;
	struct cifs_pending_open *pending_open;
	unsigned int epoch;
#ifdef CONFIG_CIFS_DEBUG2
	__u64 mid;
#endif /* CIFS_DEBUG2 */
	bool purge_cache;
};

struct cifs_fid_locks {
	struct list_head llist;
	struct cifsFileInfo *cfile;	/* fid that owns locks */
	struct list_head locks;		/* locks held by fid above */
};

struct cifsFileInfo {
	/* following two lists are protected by tcon->open_file_lock */
	struct list_head tlist;	/* pointer to next fid owned by tcon */
	struct list_head flist;	/* next fid (file instance) for this inode */
	/* lock list below protected by cifsi->lock_sem */
	struct cifs_fid_locks *llist;	/* brlocks held by this fid */
	kuid_t uid;		/* allows finding which FileInfo structure */
	__u32 pid;		/* process id who opened file */
	struct cifs_fid fid;	/* file id from remote */
	struct list_head rlist; /* reconnect list */
	/* BB add lock scope info here if needed */
	/* lock scope id (0 if none) */
	struct dentry *dentry;
	struct tcon_link *tlink;
	unsigned int f_flags;
	bool invalidHandle:1;	/* file closed via session abend */
	bool swapfile:1;
	bool oplock_break_cancelled:1;
	bool status_file_deleted:1; /* file has been deleted */
	bool offload:1; /* offload final part of _put to a wq */
	unsigned int oplock_epoch; /* epoch from the lease break */
	__u32 oplock_level; /* oplock/lease level from the lease break */
	int count;
	spinlock_t file_info_lock; /* protects four flag/count fields above */
	struct mutex fh_mutex; /* prevents reopen race after dead ses*/
	struct cifs_search_info srch_inf;
	struct work_struct oplock_break; /* work for oplock breaks */
	struct work_struct put; /* work for the final part of _put */
	struct work_struct serverclose; /* work for serverclose */
	struct delayed_work deferred;
	bool deferred_close_scheduled; /* Flag to indicate close is scheduled */
	char *symlink_target;
};

struct cifs_io_parms {
	__u16 netfid;
	__u64 persistent_fid;	/* persist file id for smb2 */
	__u64 volatile_fid;	/* volatile file id for smb2 */
	__u32 pid;
	__u64 offset;
	unsigned int length;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
};

struct cifs_io_request {
	struct netfs_io_request		rreq;
	struct cifsFileInfo		*cfile;
	struct TCP_Server_Info		*server;
	pid_t				pid;
};

/* asynchronous read support */
struct cifs_io_subrequest {
	union {
		struct netfs_io_subrequest subreq;
		struct netfs_io_request *rreq;
		struct cifs_io_request *req;
	};
	ssize_t				got_bytes;
	unsigned int			xid;
	int				result;
	bool				have_xid;
	bool				replay;
	struct kvec			iov[2];
	struct TCP_Server_Info		*server;
#ifdef CONFIG_CIFS_SMB_DIRECT
	struct smbd_mr			*mr;
#endif
	struct cifs_credits		credits;
};

/*
 * Take a reference on the file private data. Must be called with
 * cfile->file_info_lock held.
 */
static inline void
cifsFileInfo_get_locked(struct cifsFileInfo *cifs_file)
{
	++cifs_file->count;
}

struct cifsFileInfo *cifsFileInfo_get(struct cifsFileInfo *cifs_file);
void _cifsFileInfo_put(struct cifsFileInfo *cifs_file, bool wait_oplock_hdlr,
		       bool offload);
void cifsFileInfo_put(struct cifsFileInfo *cifs_file);

#define CIFS_CACHE_READ_FLG	1
#define CIFS_CACHE_HANDLE_FLG	2
#define CIFS_CACHE_RH_FLG	(CIFS_CACHE_READ_FLG | CIFS_CACHE_HANDLE_FLG)
#define CIFS_CACHE_WRITE_FLG	4
#define CIFS_CACHE_RW_FLG	(CIFS_CACHE_READ_FLG | CIFS_CACHE_WRITE_FLG)
#define CIFS_CACHE_RHW_FLG	(CIFS_CACHE_RW_FLG | CIFS_CACHE_HANDLE_FLG)

#define CIFS_CACHE_READ(cinode) ((cinode->oplock & CIFS_CACHE_READ_FLG) || (CIFS_SB(cinode->netfs.inode.i_sb)->mnt_cifs_flags & CIFS_MOUNT_RO_CACHE))
#define CIFS_CACHE_HANDLE(cinode) (cinode->oplock & CIFS_CACHE_HANDLE_FLG)
#define CIFS_CACHE_WRITE(cinode) ((cinode->oplock & CIFS_CACHE_WRITE_FLG) || (CIFS_SB(cinode->netfs.inode.i_sb)->mnt_cifs_flags & CIFS_MOUNT_RW_CACHE))

/*
 * One of these for each file inode
 */

struct cifsInodeInfo {
	struct netfs_inode netfs; /* Netfslib context and vfs inode */
	bool can_cache_brlcks;
	struct list_head llist;	/* locks helb by this inode */
	/*
	 * NOTE: Some code paths call down_read(lock_sem) twice, so
	 * we must always use cifs_down_write() instead of down_write()
	 * for this semaphore to avoid deadlocks.
	 */
	struct rw_semaphore lock_sem;	/* protect the fields above */
	/* BB add in lists for dirty pages i.e. write caching info for oplock */
	struct list_head openFileList;
	spinlock_t	open_file_lock;	/* protects openFileList */
	__u32 cifsAttrs; /* e.g. DOS archive bit, sparse, compressed, system */
	unsigned int oplock;		/* oplock/lease level we have */
	unsigned int epoch;		/* used to track lease state changes */
#define CIFS_INODE_PENDING_OPLOCK_BREAK   (0) /* oplock break in progress */
#define CIFS_INODE_PENDING_WRITERS	  (1) /* Writes in progress */
#define CIFS_INODE_FLAG_UNUSED		  (2) /* Unused flag */
#define CIFS_INO_DELETE_PENDING		  (3) /* delete pending on server */
#define CIFS_INO_INVALID_MAPPING	  (4) /* pagecache is invalid */
#define CIFS_INO_LOCK			  (5) /* lock bit for synchronization */
#define CIFS_INO_CLOSE_ON_LOCK            (7) /* Not to defer the close when lock is set */
	unsigned long flags;
	spinlock_t writers_lock;
	unsigned int writers;		/* Number of writers on this inode */
	unsigned long time;		/* jiffies of last update of inode */
	u64  uniqueid;			/* server inode number */
	u64  createtime;		/* creation time on server */
	__u8 lease_key[SMB2_LEASE_KEY_SIZE];	/* lease key for this inode */
	struct list_head deferred_closes; /* list of deferred closes */
	spinlock_t deferred_lock; /* protection on deferred list */
	bool lease_granted; /* Flag to indicate whether lease or oplock is granted. */
	char *symlink_target;
	__u32 reparse_tag;
};

static inline struct cifsInodeInfo *
CIFS_I(struct inode *inode)
{
	return container_of(inode, struct cifsInodeInfo, netfs.inode);
}

static inline struct cifs_sb_info *
CIFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct cifs_sb_info *
CIFS_FILE_SB(struct file *file)
{
	return CIFS_SB(file_inode(file)->i_sb);
}

static inline char CIFS_DIR_SEP(const struct cifs_sb_info *cifs_sb)
{
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_POSIX_PATHS)
		return '/';
	else
		return '\\';
}

static inline void
convert_delimiter(char *path, char delim)
{
	char old_delim, *pos;

	if (delim == '/')
		old_delim = '\\';
	else
		old_delim = '/';

	pos = path;
	while ((pos = strchr(pos, old_delim)))
		*pos = delim;
}

#define cifs_stats_inc atomic_inc

static inline void cifs_stats_bytes_written(struct cifs_tcon *tcon,
					    unsigned int bytes)
{
	if (bytes) {
		spin_lock(&tcon->stat_lock);
		tcon->bytes_written += bytes;
		spin_unlock(&tcon->stat_lock);
	}
}

static inline void cifs_stats_bytes_read(struct cifs_tcon *tcon,
					 unsigned int bytes)
{
	spin_lock(&tcon->stat_lock);
	tcon->bytes_read += bytes;
	spin_unlock(&tcon->stat_lock);
}


/*
 * This is the prototype for the mid receive function. This function is for
 * receiving the rest of the SMB frame, starting with the WordCount (which is
 * just after the MID in struct smb_hdr). Note:
 *
 * - This will be called by cifsd, with no locks held.
 * - The mid will still be on the pending_mid_q.
 * - mid->resp_buf will point to the current buffer.
 *
 * Returns zero on a successful receive, or an error. The receive state in
 * the TCP_Server_Info will also be updated.
 */
typedef int (mid_receive_t)(struct TCP_Server_Info *server,
			    struct mid_q_entry *mid);

/*
 * This is the prototype for the mid callback function. This is called once the
 * mid has been received off of the socket. When creating one, take special
 * care to avoid deadlocks. Things to bear in mind:
 *
 * - it will be called by cifsd, with no locks held
 * - the mid will be removed from any lists
 */
typedef void (mid_callback_t)(struct mid_q_entry *mid);

/*
 * This is the protopyte for mid handle function. This is called once the mid
 * has been recognized after decryption of the message.
 */
typedef int (mid_handle_t)(struct TCP_Server_Info *server,
			    struct mid_q_entry *mid);

/* one of these for every pending CIFS request to the server */
struct mid_q_entry {
	struct list_head qhead;	/* mids waiting on reply from this server */
	struct kref refcount;
	struct TCP_Server_Info *server;	/* server corresponding to this mid */
	__u64 mid;		/* multiplex id */
	__u16 credits;		/* number of credits consumed by this mid */
	__u16 credits_received;	/* number of credits from the response */
	__u32 pid;		/* process id */
	__u32 sequence_number;  /* for CIFS signing */
	unsigned long when_alloc;  /* when mid was created */
#ifdef CONFIG_CIFS_STATS2
	unsigned long when_sent; /* time when smb send finished */
	unsigned long when_received; /* when demux complete (taken off wire) */
#endif
	mid_receive_t *receive; /* call receive callback */
	mid_callback_t *callback; /* call completion callback */
	mid_handle_t *handle; /* call handle mid callback */
	void *callback_data;	  /* general purpose pointer for callback */
	struct task_struct *creator;
	void *resp_buf;		/* pointer to received SMB header */
	unsigned int resp_buf_size;
	int mid_state;	/* wish this were enum but can not pass to wait_event */
	unsigned int mid_flags;
	__le16 command;		/* smb command code */
	unsigned int optype;	/* operation type */
	bool large_buf:1;	/* if valid response, is pointer to large buf */
	bool multiRsp:1;	/* multiple trans2 responses for one request  */
	bool multiEnd:1;	/* both received */
	bool decrypted:1;	/* decrypted entry */
};

struct close_cancelled_open {
	struct cifs_fid         fid;
	struct cifs_tcon        *tcon;
	struct work_struct      work;
	__u64 mid;
	__u16 cmd;
};

/*	Make code in transport.c a little cleaner by moving
	update of optional stats into function below */
static inline void cifs_in_send_inc(struct TCP_Server_Info *server)
{
	atomic_inc(&server->in_send);
}

static inline void cifs_in_send_dec(struct TCP_Server_Info *server)
{
	atomic_dec(&server->in_send);
}

static inline void cifs_num_waiters_inc(struct TCP_Server_Info *server)
{
	atomic_inc(&server->num_waiters);
}

static inline void cifs_num_waiters_dec(struct TCP_Server_Info *server)
{
	atomic_dec(&server->num_waiters);
}

#ifdef CONFIG_CIFS_STATS2
static inline void cifs_save_when_sent(struct mid_q_entry *mid)
{
	mid->when_sent = jiffies;
}
#else
static inline void cifs_save_when_sent(struct mid_q_entry *mid)
{
}
#endif

/* for pending dnotify requests */
struct dir_notify_req {
	struct list_head lhead;
	__le16 Pid;
	__le16 PidHigh;
	__u16 Mid;
	__u16 Tid;
	__u16 Uid;
	__u16 netfid;
	__u32 filter; /* CompletionFilter (for multishot) */
	int multishot;
	struct file *pfile;
};

struct dfs_info3_param {
	int flags; /* DFSREF_REFERRAL_SERVER, DFSREF_STORAGE_SERVER*/
	int path_consumed;
	int server_type;
	int ref_flag;
	char *path_name;
	char *node_name;
	int ttl;
};

struct file_list {
	struct list_head list;
	struct cifsFileInfo *cfile;
};

struct cifs_mount_ctx {
	struct cifs_sb_info *cifs_sb;
	struct smb3_fs_context *fs_ctx;
	unsigned int xid;
	struct TCP_Server_Info *server;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;
};

static inline void __free_dfs_info_param(struct dfs_info3_param *param)
{
	kfree(param->path_name);
	kfree(param->node_name);
}

static inline void free_dfs_info_param(struct dfs_info3_param *param)
{
	if (param)
		__free_dfs_info_param(param);
}

static inline void zfree_dfs_info_param(struct dfs_info3_param *param)
{
	if (param) {
		__free_dfs_info_param(param);
		memset(param, 0, sizeof(*param));
	}
}

static inline void free_dfs_info_array(struct dfs_info3_param *param,
				       int number_of_items)
{
	int i;

	if ((number_of_items == 0) || (param == NULL))
		return;
	for (i = 0; i < number_of_items; i++) {
		kfree(param[i].path_name);
		kfree(param[i].node_name);
	}
	kfree(param);
}

static inline bool is_interrupt_error(int error)
{
	switch (error) {
	case -EINTR:
	case -ERESTARTSYS:
	case -ERESTARTNOHAND:
	case -ERESTARTNOINTR:
		return true;
	}
	return false;
}

static inline bool is_retryable_error(int error)
{
	if (is_interrupt_error(error) || error == -EAGAIN)
		return true;
	return false;
}

static inline bool is_replayable_error(int error)
{
	if (error == -EAGAIN || error == -ECONNABORTED)
		return true;
	return false;
}


/* cifs_get_writable_file() flags */
#define FIND_WR_ANY         0
#define FIND_WR_FSUID_ONLY  1
#define FIND_WR_WITH_DELETE 2

#define   MID_FREE 0
#define   MID_REQUEST_ALLOCATED 1
#define   MID_REQUEST_SUBMITTED 2
#define   MID_RESPONSE_RECEIVED 4
#define   MID_RETRY_NEEDED      8 /* session closed while this request out */
#define   MID_RESPONSE_MALFORMED 0x10
#define   MID_SHUTDOWN		 0x20
#define   MID_RESPONSE_READY 0x40 /* ready for other process handle the rsp */

/* Flags */
#define   MID_WAIT_CANCELLED	 1 /* Cancelled while waiting for response */
#define   MID_DELETED            2 /* Mid has been dequeued/deleted */

/* Types of response buffer returned from SendReceive2 */
#define   CIFS_NO_BUFFER        0    /* Response buffer not returned */
#define   CIFS_SMALL_BUFFER     1
#define   CIFS_LARGE_BUFFER     2
#define   CIFS_IOVEC            4    /* array of response buffers */

/* Type of Request to SendReceive2 */
#define   CIFS_BLOCKING_OP      1    /* operation can block */
#define   CIFS_NON_BLOCKING     2    /* do not block waiting for credits */
#define   CIFS_TIMEOUT_MASK 0x003    /* only one of above set in req */
#define   CIFS_LOG_ERROR    0x010    /* log NT STATUS if non-zero */
#define   CIFS_LARGE_BUF_OP 0x020    /* large request buffer */
#define   CIFS_NO_RSP_BUF   0x040    /* no response buffer required */

/* Type of request operation */
#define   CIFS_ECHO_OP            0x080  /* echo request */
#define   CIFS_OBREAK_OP          0x0100 /* oplock break request */
#define   CIFS_NEG_OP             0x0200 /* negotiate request */
#define   CIFS_CP_CREATE_CLOSE_OP 0x0400 /* compound create+close request */
/* Lower bitmask values are reserved by others below. */
#define   CIFS_SESS_OP            0x2000 /* session setup request */
#define   CIFS_OP_MASK            0x2780 /* mask request type */

#define   CIFS_HAS_CREDITS        0x0400 /* already has credits */
#define   CIFS_TRANSFORM_REQ      0x0800 /* transform request before sending */
#define   CIFS_NO_SRV_RSP         0x1000 /* there is no server response */
#define   CIFS_COMPRESS_REQ       0x4000 /* compress request before sending */

/* Security Flags: indicate type of session setup needed */
#define   CIFSSEC_MAY_SIGN	0x00001
#define   CIFSSEC_MAY_NTLMV2	0x00004
#define   CIFSSEC_MAY_KRB5	0x00008
#define   CIFSSEC_MAY_SEAL	0x00040
#define   CIFSSEC_MAY_NTLMSSP	0x00080 /* raw ntlmssp with ntlmv2 */

#define   CIFSSEC_MUST_SIGN	0x01001
/* note that only one of the following can be set so the
result of setting MUST flags more than once will be to
require use of the stronger protocol */
#define   CIFSSEC_MUST_NTLMV2	0x04004
#define   CIFSSEC_MUST_KRB5	0x08008
#ifdef CONFIG_CIFS_UPCALL
#define   CIFSSEC_MASK          0xCF0CF /* flags supported if no weak allowed */
#else
#define	  CIFSSEC_MASK          0xC70C7 /* flags supported if no weak allowed */
#endif /* UPCALL */
#define   CIFSSEC_MUST_SEAL	0x40040
#define   CIFSSEC_MUST_NTLMSSP	0x80080 /* raw ntlmssp with ntlmv2 */

#define   CIFSSEC_DEF (CIFSSEC_MAY_SIGN | CIFSSEC_MAY_NTLMV2 | CIFSSEC_MAY_NTLMSSP | CIFSSEC_MAY_SEAL)
#define   CIFSSEC_MAX (CIFSSEC_MAY_SIGN | CIFSSEC_MUST_KRB5 | CIFSSEC_MAY_SEAL)
#define   CIFSSEC_AUTH_MASK (CIFSSEC_MAY_NTLMV2 | CIFSSEC_MAY_KRB5 | CIFSSEC_MAY_NTLMSSP)
/*
 *****************************************************************
 * All constants go here
 *****************************************************************
 */

#define UID_HASH (16)

/*
 * Note that ONE module should define _DECLARE_GLOBALS_HERE to cause the
 * following to be declared.
 */

/****************************************************************************
 * Here are all the locks (spinlock, mutex, semaphore) in cifs.ko, arranged according
 * to the locking order. i.e. if two locks are to be held together, the lock that
 * appears higher in this list needs to be taken before the other.
 *
 * If you hold a lock that is lower in this list, and you need to take a higher lock
 * (or if you think that one of the functions that you're calling may need to), first
 * drop the lock you hold, pick up the higher lock, then the lower one. This will
 * ensure that locks are picked up only in one direction in the below table
 * (top to bottom).
 *
 * Also, if you expect a function to be called with a lock held, explicitly document
 * this in the comments on top of your function definition.
 *
 * And also, try to keep the critical sections (lock hold time) to be as minimal as
 * possible. Blocking / calling other functions with a lock held always increase
 * the risk of a possible deadlock.
 *
 * Following this rule will avoid unnecessary deadlocks, which can get really hard to
 * debug. Also, any new lock that you introduce, please add to this list in the correct
 * order.
 *
 * Please populate this list whenever you introduce new locks in your changes. Or in
 * case I've missed some existing locks. Please ensure that it's added in the list
 * based on the locking order expected.
 *
 * =====================================================================================
 * Lock				Protects			Initialization fn
 * =====================================================================================
 * vol_list_lock
 * vol_info->ctx_lock		vol_info->ctx
 * cifs_sb_info->tlink_tree_lock	cifs_sb_info->tlink_tree	cifs_setup_cifs_sb
 * TCP_Server_Info->		TCP_Server_Info			cifs_get_tcp_session
 * reconnect_mutex
 * TCP_Server_Info->srv_mutex	TCP_Server_Info			cifs_get_tcp_session
 * cifs_ses->session_mutex		cifs_ses		sesInfoAlloc
 *				cifs_tcon
 * cifs_tcon->open_file_lock	cifs_tcon->openFileList		tconInfoAlloc
 *				cifs_tcon->pending_opens
 * cifs_tcon->stat_lock		cifs_tcon->bytes_read		tconInfoAlloc
 *				cifs_tcon->bytes_written
 * cifs_tcp_ses_lock		cifs_tcp_ses_list		sesInfoAlloc
 * GlobalMid_Lock		GlobalMaxActiveXid		init_cifs
 *				GlobalCurrentXid
 *				GlobalTotalActiveXid
 * TCP_Server_Info->srv_lock	(anything in struct not protected by another lock and can change)
 * TCP_Server_Info->mid_lock	TCP_Server_Info->pending_mid_q	cifs_get_tcp_session
 *				->CurrentMid
 *				(any changes in mid_q_entry fields)
 * TCP_Server_Info->req_lock	TCP_Server_Info->in_flight	cifs_get_tcp_session
 *				->credits
 *				->echo_credits
 *				->oplock_credits
 *				->reconnect_instance
 * cifs_ses->ses_lock		(anything that is not protected by another lock and can change)
 * cifs_ses->iface_lock		cifs_ses->iface_list		sesInfoAlloc
 *				->iface_count
 *				->iface_last_update
 * cifs_ses->chan_lock		cifs_ses->chans
 *				->chans_need_reconnect
 *				->chans_in_reconnect
 * cifs_tcon->tc_lock		(anything that is not protected by another lock and can change)
 * inode->i_rwsem, taken by fs/netfs/locking.c e.g. should be taken before cifsInodeInfo locks
 * cifsInodeInfo->open_file_lock	cifsInodeInfo->openFileList	cifs_alloc_inode
 * cifsInodeInfo->writers_lock	cifsInodeInfo->writers		cifsInodeInfo_alloc
 * cifsInodeInfo->lock_sem	cifsInodeInfo->llist		cifs_init_once
 *				->can_cache_brlcks
 * cifsInodeInfo->deferred_lock	cifsInodeInfo->deferred_closes	cifsInodeInfo_alloc
 * cached_fids->cfid_list_lock	cifs_tcon->cfids->entries	 init_cached_dirs
 * cifsFileInfo->fh_mutex		cifsFileInfo			cifs_new_fileinfo
 * cifsFileInfo->file_info_lock	cifsFileInfo->count		cifs_new_fileinfo
 *				->invalidHandle			initiate_cifs_search
 *				->oplock_break_cancelled
 ****************************************************************************/

#ifdef DECLARE_GLOBALS_HERE
#define GLOBAL_EXTERN
#else
#define GLOBAL_EXTERN extern
#endif

/*
 * the list of TCP_Server_Info structures, ie each of the sockets
 * connecting our client to a distinct server (ip address), is
 * chained together by cifs_tcp_ses_list. The list of all our SMB
 * sessions (and from that the tree connections) can be found
 * by iterating over cifs_tcp_ses_list
 */
extern struct list_head		cifs_tcp_ses_list;

/*
 * This lock protects the cifs_tcp_ses_list, the list of smb sessions per
 * tcp session, and the list of tcon's per smb session. It also protects
 * the reference counters for the server, smb session, and tcon.
 * generally the locks should be taken in order tcp_ses_lock before
 * tcon->open_file_lock and that before file->file_info_lock since the
 * structure order is cifs_socket-->cifs_ses-->cifs_tcon-->cifs_file
 */
extern spinlock_t		cifs_tcp_ses_lock;

/*
 * Global transaction id (XID) information
 */
extern unsigned int GlobalCurrentXid;	/* protected by GlobalMid_Lock */
extern unsigned int GlobalTotalActiveXid; /* prot by GlobalMid_Lock */
extern unsigned int GlobalMaxActiveXid;	/* prot by GlobalMid_Lock */
extern spinlock_t GlobalMid_Lock; /* protects above & list operations on midQ entries */

/*
 *  Global counters, updated atomically
 */
extern atomic_t sesInfoAllocCount;
extern atomic_t tconInfoAllocCount;
extern atomic_t tcpSesNextId;
extern atomic_t tcpSesAllocCount;
extern atomic_t tcpSesReconnectCount;
extern atomic_t tconInfoReconnectCount;

/* Various Debug counters */
extern atomic_t buf_alloc_count;	/* current number allocated  */
extern atomic_t small_buf_alloc_count;
#ifdef CONFIG_CIFS_STATS2
extern atomic_t total_buf_alloc_count; /* total allocated over all time */
extern atomic_t total_small_buf_alloc_count;
extern unsigned int slow_rsp_threshold; /* number of secs before logging */
#endif

/* Misc globals */
extern bool enable_oplocks; /* enable or disable oplocks */
extern bool lookupCacheEnabled;
extern unsigned int global_secflags;	/* if on, session setup sent
				with more secure ntlmssp2 challenge/resp */
extern unsigned int sign_CIFS_PDUs;  /* enable smb packet signing */
extern bool enable_gcm_256; /* allow optional negotiate of strongest signing (aes-gcm-256) */
extern bool require_gcm_256; /* require use of strongest signing (aes-gcm-256) */
extern bool enable_negotiate_signing; /* request use of faster (GMAC) signing if available */
extern bool linuxExtEnabled;/*enable Linux/Unix CIFS extensions*/
extern unsigned int CIFSMaxBufSize;  /* max size not including hdr */
extern unsigned int cifs_min_rcv;    /* min size of big ntwrk buf pool */
extern unsigned int cifs_min_small;  /* min size of small buf pool */
extern unsigned int cifs_max_pending; /* MAX requests at once to server*/
extern unsigned int dir_cache_timeout; /* max time for directory lease caching of dir */
extern bool disable_legacy_dialects;  /* forbid vers=1.0 and vers=2.0 mounts */
extern atomic_t mid_count;

void cifs_oplock_break(struct work_struct *work);
void cifs_queue_oplock_break(struct cifsFileInfo *cfile);
void smb2_deferred_work_close(struct work_struct *work);

extern const struct slow_work_ops cifs_oplock_break_ops;
extern struct workqueue_struct *cifsiod_wq;
extern struct workqueue_struct *decrypt_wq;
extern struct workqueue_struct *fileinfo_put_wq;
extern struct workqueue_struct *cifsoplockd_wq;
extern struct workqueue_struct *deferredclose_wq;
extern struct workqueue_struct *serverclose_wq;
extern struct workqueue_struct *cfid_put_wq;
extern __u32 cifs_lock_secret;

extern mempool_t *cifs_sm_req_poolp;
extern mempool_t *cifs_req_poolp;
extern mempool_t *cifs_mid_poolp;
extern mempool_t cifs_io_request_pool;
extern mempool_t cifs_io_subrequest_pool;

/* Operations for different SMB versions */
#define SMB1_VERSION_STRING	"1.0"
#define SMB20_VERSION_STRING    "2.0"
#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
extern struct smb_version_operations smb1_operations;
extern struct smb_version_values smb1_values;
extern struct smb_version_operations smb20_operations;
extern struct smb_version_values smb20_values;
#endif /* CIFS_ALLOW_INSECURE_LEGACY */
#define SMB21_VERSION_STRING	"2.1"
extern struct smb_version_operations smb21_operations;
extern struct smb_version_values smb21_values;
#define SMBDEFAULT_VERSION_STRING "default"
extern struct smb_version_values smbdefault_values;
#define SMB3ANY_VERSION_STRING "3"
extern struct smb_version_values smb3any_values;
#define SMB30_VERSION_STRING	"3.0"
extern struct smb_version_operations smb30_operations;
extern struct smb_version_values smb30_values;
#define SMB302_VERSION_STRING	"3.02"
#define ALT_SMB302_VERSION_STRING "3.0.2"
/*extern struct smb_version_operations smb302_operations;*/ /* not needed yet */
extern struct smb_version_values smb302_values;
#define SMB311_VERSION_STRING	"3.1.1"
#define ALT_SMB311_VERSION_STRING "3.11"
extern struct smb_version_operations smb311_operations;
extern struct smb_version_values smb311_values;

static inline char *get_security_type_str(enum securityEnum sectype)
{
	switch (sectype) {
	case RawNTLMSSP:
		return "RawNTLMSSP";
	case Kerberos:
		return "Kerberos";
	case NTLMv2:
		return "NTLMv2";
	default:
		return "Unknown";
	}
}

static inline bool is_smb1_server(struct TCP_Server_Info *server)
{
	return strcmp(server->vals->version_string, SMB1_VERSION_STRING) == 0;
}

static inline bool is_tcon_dfs(struct cifs_tcon *tcon)
{
	/*
	 * For SMB1, see MS-CIFS 2.4.55 SMB_COM_TREE_CONNECT_ANDX (0x75) and MS-CIFS 3.3.4.4 DFS
	 * Subsystem Notifies That a Share Is a DFS Share.
	 *
	 * For SMB2+, see MS-SMB2 2.2.10 SMB2 TREE_CONNECT Response and MS-SMB2 3.3.4.14 Server
	 * Application Updates a Share.
	 */
	if (!tcon || !tcon->ses || !tcon->ses->server)
		return false;
	return is_smb1_server(tcon->ses->server) ? tcon->Flags & SMB_SHARE_IS_IN_DFS :
		tcon->share_flags & (SHI1005_FLAGS_DFS | SHI1005_FLAGS_DFS_ROOT);
}

static inline bool cifs_is_referral_server(struct cifs_tcon *tcon,
					   const struct dfs_info3_param *ref)
{
	/*
	 * Check if all targets are capable of handling DFS referrals as per
	 * MS-DFSC 2.2.4 RESP_GET_DFS_REFERRAL.
	 */
	return is_tcon_dfs(tcon) || (ref && (ref->flags & DFSREF_REFERRAL_SERVER));
}

static inline u64 cifs_flock_len(const struct file_lock *fl)
{
	return (u64)fl->fl_end - fl->fl_start + 1;
}

static inline size_t ntlmssp_workstation_name_size(const struct cifs_ses *ses)
{
	if (WARN_ON_ONCE(!ses || !ses->server))
		return 0;
	/*
	 * Make workstation name no more than 15 chars when using insecure dialects as some legacy
	 * servers do require it during NTLMSSP.
	 */
	if (ses->server->dialect <= SMB20_PROT_ID)
		return min_t(size_t, sizeof(ses->workstation_name), RFC1001_NAME_LEN_WITH_NULL);
	return sizeof(ses->workstation_name);
}

static inline void move_cifs_info_to_smb2(struct smb2_file_all_info *dst, const FILE_ALL_INFO *src)
{
	memcpy(dst, src, (size_t)((u8 *)&src->AccessFlags - (u8 *)src));
	dst->AccessFlags = src->AccessFlags;
	dst->CurrentByteOffset = src->CurrentByteOffset;
	dst->Mode = src->Mode;
	dst->AlignmentRequirement = src->AlignmentRequirement;
	dst->FileNameLength = src->FileNameLength;
}

static inline int cifs_get_num_sgs(const struct smb_rqst *rqst,
				   int num_rqst,
				   const u8 *sig)
{
	unsigned int len, skip;
	unsigned int nents = 0;
	unsigned long addr;
	size_t data_size;
	int i, j;

	/*
	 * The first rqst has a transform header where the first 20 bytes are
	 * not part of the encrypted blob.
	 */
	skip = 20;

	/* Assumes the first rqst has a transform header as the first iov.
	 * I.e.
	 * rqst[0].rq_iov[0]  is transform header
	 * rqst[0].rq_iov[1+] data to be encrypted/decrypted
	 * rqst[1+].rq_iov[0+] data to be encrypted/decrypted
	 */
	for (i = 0; i < num_rqst; i++) {
		data_size = iov_iter_count(&rqst[i].rq_iter);

		/* We really don't want a mixture of pinned and unpinned pages
		 * in the sglist.  It's hard to keep track of which is what.
		 * Instead, we convert to a BVEC-type iterator higher up.
		 */
		if (data_size &&
		    WARN_ON_ONCE(user_backed_iter(&rqst[i].rq_iter)))
			return -EIO;

		/* We also don't want to have any extra refs or pins to clean
		 * up in the sglist.
		 */
		if (data_size &&
		    WARN_ON_ONCE(iov_iter_extract_will_pin(&rqst[i].rq_iter)))
			return -EIO;

		for (j = 0; j < rqst[i].rq_nvec; j++) {
			struct kvec *iov = &rqst[i].rq_iov[j];

			addr = (unsigned long)iov->iov_base + skip;
			if (unlikely(is_vmalloc_addr((void *)addr))) {
				len = iov->iov_len - skip;
				nents += DIV_ROUND_UP(offset_in_page(addr) + len,
						      PAGE_SIZE);
			} else {
				nents++;
			}
			skip = 0;
		}
		if (data_size)
			nents += iov_iter_npages(&rqst[i].rq_iter, INT_MAX);
	}
	nents += DIV_ROUND_UP(offset_in_page(sig) + SMB2_SIGNATURE_SIZE, PAGE_SIZE);
	return nents;
}

/* We can not use the normal sg_set_buf() as we will sometimes pass a
 * stack object as buf.
 */
static inline void cifs_sg_set_buf(struct sg_table *sgtable,
				   const void *buf,
				   unsigned int buflen)
{
	unsigned long addr = (unsigned long)buf;
	unsigned int off = offset_in_page(addr);

	addr &= PAGE_MASK;
	if (unlikely(is_vmalloc_addr((void *)addr))) {
		do {
			unsigned int len = min_t(unsigned int, buflen, PAGE_SIZE - off);

			sg_set_page(&sgtable->sgl[sgtable->nents++],
				    vmalloc_to_page((void *)addr), len, off);

			off = 0;
			addr += PAGE_SIZE;
			buflen -= len;
		} while (buflen);
	} else {
		sg_set_page(&sgtable->sgl[sgtable->nents++],
			    virt_to_page((void *)addr), buflen, off);
	}
}

#define CIFS_OPARMS(_cifs_sb, _tcon, _path, _da, _cd, _co, _mode) \
	((struct cifs_open_parms) { \
		.tcon = _tcon, \
		.path = _path, \
		.desired_access = (_da), \
		.disposition = (_cd), \
		.create_options = cifs_create_options(_cifs_sb, (_co)), \
		.mode = (_mode), \
		.cifs_sb = _cifs_sb, \
	})

struct smb2_compound_vars {
	struct cifs_open_parms oparms;
	struct kvec rsp_iov[MAX_COMPOUND];
	struct smb_rqst rqst[MAX_COMPOUND];
	struct kvec open_iov[SMB2_CREATE_IOV_SIZE];
	struct kvec qi_iov;
	struct kvec io_iov[SMB2_IOCTL_IOV_SIZE];
	struct kvec si_iov[SMB2_SET_INFO_IOV_SIZE];
	struct kvec close_iov;
	struct smb2_file_rename_info rename_info;
	struct smb2_file_link_info link_info;
	struct kvec ea_iov;
};

static inline bool cifs_ses_exiting(struct cifs_ses *ses)
{
	bool ret;

	spin_lock(&ses->ses_lock);
	ret = ses->ses_status == SES_EXITING;
	spin_unlock(&ses->ses_lock);
	return ret;
}

#endif	/* _CIFS_GLOB_H */
