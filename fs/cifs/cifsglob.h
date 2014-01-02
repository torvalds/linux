/*
 *   fs/cifs/cifsglob.h
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Jeremy Allison (jra@samba.org)
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 */
#ifndef _CIFS_GLOB_H
#define _CIFS_GLOB_H

#include <linux/in.h>
#include <linux/in6.h>
#include <linux/slab.h>
#include <linux/mempool.h>
#include <linux/workqueue.h>
#include "cifs_fs_sb.h"
#include "cifsacl.h"
#include <crypto/internal/hash.h>
#include <linux/scatterlist.h>
#include <uapi/linux/cifs/cifs_mount.h>
#ifdef CONFIG_CIFS_SMB2
#include "smb2pdu.h"
#endif

#define CIFS_MAGIC_NUMBER 0xFF534D42      /* the first four bytes of SMB PDUs */

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
 * max attribute cache timeout (jiffies) - 2^30
 */
#define CIFS_MAX_ACTIMEO (1 << 30)

/*
 * MAX_REQ is the maximum number of requests that WE will send
 * on one socket concurrently.
 */
#define CIFS_MAX_REQ 32767

#define RFC1001_NAME_LEN 15
#define RFC1001_NAME_LEN_WITH_NULL (RFC1001_NAME_LEN + 1)

/* currently length of NIP6_FMT */
#define SERVER_NAME_LENGTH 40
#define SERVER_NAME_LEN_WITH_NULL     (SERVER_NAME_LENGTH + 1)

/* used to define string lengths for reversing unicode strings */
/*         (256+1)*2 = 514                                     */
/*           (max path length + 1 for null) * 2 for unicode    */
#define MAX_NAME 514

/* SMB echo "timeout" -- FIXME: tunable? */
#define SMB_ECHO_INTERVAL (60 * HZ)

#include "cifspdu.h"

#ifndef XATTR_DOS_ATTRIB
#define XATTR_DOS_ATTRIB "user.DOSATTRIB"
#endif

/*
 * CIFS vfs client Status information (based on what we know.)
 */

/* associated with each tcp and smb session */
enum statusEnum {
	CifsNew = 0,
	CifsGood,
	CifsExiting,
	CifsNeedReconnect,
	CifsNeedNegotiate
};

enum securityEnum {
	Unspecified = 0,	/* not specified */
	LANMAN,			/* Legacy LANMAN auth */
	NTLM,			/* Legacy NTLM012 auth with NTLM hash */
	NTLMv2,			/* Legacy NTLM auth with NTLMv2 hash */
	RawNTLMSSP,		/* NTLMSSP without SPNEGO, NTLMv2 hash */
	Kerberos,		/* Kerberos via SPNEGO */
};

struct session_key {
	unsigned int len;
	char *response;
};

/* crypto security descriptor definition */
struct sdesc {
	struct shash_desc shash;
	char ctx[];
};

/* crypto hashing related structure/fields, not specific to a sec mech */
struct cifs_secmech {
	struct crypto_shash *hmacmd5; /* hmac-md5 hash function */
	struct crypto_shash *md5; /* md5 hash function */
	struct crypto_shash *hmacsha256; /* hmac-sha256 hash function */
	struct crypto_shash *cmacaes; /* block-cipher based MAC function */
	struct sdesc *sdeschmacmd5;  /* ctxt to generate ntlmv2 hash, CR1 */
	struct sdesc *sdescmd5; /* ctxt to generate cifs/smb signature */
	struct sdesc *sdeschmacsha256;  /* ctxt to generate smb2 signature */
	struct sdesc *sdesccmacaes;  /* ctxt to generate smb3 signature */
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
	struct cifs_sid osid;
	struct cifs_sid gsid;
	struct cifs_ntace *ntaces;
	struct cifs_ace *aces;
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
	struct page	**rq_pages;	/* pointer to array of page ptrs */
	unsigned int	rq_npages;	/* number pages in array */
	unsigned int	rq_pagesz;	/* page size to use */
	unsigned int	rq_tailsz;	/* length of last page */
};

enum smb_version {
	Smb_1 = 1,
	Smb_20,
	Smb_21,
	Smb_30,
	Smb_302,
};

struct mid_q_entry;
struct TCP_Server_Info;
struct cifsFileInfo;
struct cifs_ses;
struct cifs_tcon;
struct dfs_info3_param;
struct cifs_fattr;
struct smb_vol;
struct cifs_fid;
struct cifs_readdata;
struct cifs_writedata;
struct cifs_io_parms;
struct cifs_search_info;
struct cifsInodeInfo;
struct cifs_open_parms;

struct smb_version_operations {
	int (*send_cancel)(struct TCP_Server_Info *, void *,
			   struct mid_q_entry *);
	bool (*compare_fids)(struct cifsFileInfo *, struct cifsFileInfo *);
	/* setup request: allocate mid, sign message */
	struct mid_q_entry *(*setup_request)(struct cifs_ses *,
						struct smb_rqst *);
	/* setup async request: allocate mid, sign message */
	struct mid_q_entry *(*setup_async_request)(struct TCP_Server_Info *,
						struct smb_rqst *);
	/* check response: verify signature, map error */
	int (*check_receive)(struct mid_q_entry *, struct TCP_Server_Info *,
			     bool);
	void (*add_credits)(struct TCP_Server_Info *, const unsigned int,
			    const int);
	void (*set_credits)(struct TCP_Server_Info *, const int);
	int * (*get_credits_field)(struct TCP_Server_Info *, const int);
	unsigned int (*get_credits)(struct mid_q_entry *);
	__u64 (*get_next_mid)(struct TCP_Server_Info *);
	/* data offset from read response message */
	unsigned int (*read_data_offset)(char *);
	/* data length from read response message */
	unsigned int (*read_data_length)(char *);
	/* map smb to linux error */
	int (*map_error)(char *, bool);
	/* find mid corresponding to the response message */
	struct mid_q_entry * (*find_mid)(struct TCP_Server_Info *, char *);
	void (*dump_detail)(void *);
	void (*clear_stats)(struct cifs_tcon *);
	void (*print_stats)(struct seq_file *m, struct cifs_tcon *);
	void (*dump_share_caps)(struct seq_file *, struct cifs_tcon *);
	/* verify the message */
	int (*check_message)(char *, unsigned int);
	bool (*is_oplock_break)(char *, struct TCP_Server_Info *);
	/* process transaction2 response */
	bool (*check_trans2)(struct mid_q_entry *, struct TCP_Server_Info *,
			     char *, int);
	/* check if we need to negotiate */
	bool (*need_neg)(struct TCP_Server_Info *);
	/* negotiate to the server */
	int (*negotiate)(const unsigned int, struct cifs_ses *);
	/* set negotiated write size */
	unsigned int (*negotiate_wsize)(struct cifs_tcon *, struct smb_vol *);
	/* set negotiated read size */
	unsigned int (*negotiate_rsize)(struct cifs_tcon *, struct smb_vol *);
	/* setup smb sessionn */
	int (*sess_setup)(const unsigned int, struct cifs_ses *,
			  const struct nls_table *);
	/* close smb session */
	int (*logoff)(const unsigned int, struct cifs_ses *);
	/* connect to a server share */
	int (*tree_connect)(const unsigned int, struct cifs_ses *, const char *,
			    struct cifs_tcon *, const struct nls_table *);
	/* close tree connecion */
	int (*tree_disconnect)(const unsigned int, struct cifs_tcon *);
	/* get DFS referrals */
	int (*get_dfs_refer)(const unsigned int, struct cifs_ses *,
			     const char *, struct dfs_info3_param **,
			     unsigned int *, const struct nls_table *, int);
	/* informational QFS call */
	void (*qfs_tcon)(const unsigned int, struct cifs_tcon *);
	/* check if a path is accessible or not */
	int (*is_path_accessible)(const unsigned int, struct cifs_tcon *,
				  struct cifs_sb_info *, const char *);
	/* query path data from the server */
	int (*query_path_info)(const unsigned int, struct cifs_tcon *,
			       struct cifs_sb_info *, const char *,
			       FILE_ALL_INFO *, bool *, bool *);
	/* query file data from the server */
	int (*query_file_info)(const unsigned int, struct cifs_tcon *,
			       struct cifs_fid *, FILE_ALL_INFO *);
	/* get server index number */
	int (*get_srv_inum)(const unsigned int, struct cifs_tcon *,
			    struct cifs_sb_info *, const char *,
			    u64 *uniqueid, FILE_ALL_INFO *);
	/* set size by path */
	int (*set_path_size)(const unsigned int, struct cifs_tcon *,
			     const char *, __u64, struct cifs_sb_info *, bool);
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
	int (*mkdir)(const unsigned int, struct cifs_tcon *, const char *,
		     struct cifs_sb_info *);
	/* set info on created directory */
	void (*mkdir_setinfo)(struct inode *, const char *,
			      struct cifs_sb_info *, struct cifs_tcon *,
			      const unsigned int);
	/* remove directory */
	int (*rmdir)(const unsigned int, struct cifs_tcon *, const char *,
		     struct cifs_sb_info *);
	/* unlink file */
	int (*unlink)(const unsigned int, struct cifs_tcon *, const char *,
		      struct cifs_sb_info *);
	/* open, rename and delete file */
	int (*rename_pending_delete)(const char *, struct dentry *,
				     const unsigned int);
	/* send rename request */
	int (*rename)(const unsigned int, struct cifs_tcon *, const char *,
		      const char *, struct cifs_sb_info *);
	/* send create hardlink request */
	int (*create_hardlink)(const unsigned int, struct cifs_tcon *,
			       const char *, const char *,
			       struct cifs_sb_info *);
	/* query symlink target */
	int (*query_symlink)(const unsigned int, struct cifs_tcon *,
			     const char *, char **, struct cifs_sb_info *);
	/* open a file for non-posix mounts */
	int (*open)(const unsigned int, struct cifs_open_parms *,
		    __u32 *, FILE_ALL_INFO *);
	/* set fid protocol-specific info */
	void (*set_fid)(struct cifsFileInfo *, struct cifs_fid *, __u32);
	/* close a file */
	void (*close)(const unsigned int, struct cifs_tcon *,
		      struct cifs_fid *);
	/* send a flush request to the server */
	int (*flush)(const unsigned int, struct cifs_tcon *, struct cifs_fid *);
	/* async read from the server */
	int (*async_readv)(struct cifs_readdata *);
	/* async write to the server */
	int (*async_writev)(struct cifs_writedata *);
	/* sync read from the server */
	int (*sync_read)(const unsigned int, struct cifsFileInfo *,
			 struct cifs_io_parms *, unsigned int *, char **,
			 int *);
	/* sync write to the server */
	int (*sync_write)(const unsigned int, struct cifsFileInfo *,
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
	unsigned int (*calc_smb_size)(void *);
	/* check for STATUS_PENDING and process it in a positive case */
	bool (*is_status_pending)(char *, struct TCP_Server_Info *, int);
	/* send oplock break response */
	int (*oplock_response)(struct cifs_tcon *, struct cifs_fid *,
			       struct cifsInodeInfo *);
	/* query remote filesystem */
	int (*queryfs)(const unsigned int, struct cifs_tcon *,
		       struct kstatfs *);
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
	int (*generate_signingkey)(struct cifs_ses *);
	int (*calc_signature)(struct smb_rqst *, struct TCP_Server_Info *);
	int (*query_mf_symlink)(const unsigned char *, char *, unsigned int *,
				struct cifs_sb_info *, unsigned int);
	/* if we can do cache read operations */
	bool (*is_read_op)(__u32);
	/* set oplock level for the inode */
	void (*set_oplock_level)(struct cifsInodeInfo *, __u32, unsigned int,
				 bool *);
	/* create lease context buffer for CREATE request */
	char * (*create_lease_buf)(u8 *, u8);
	/* parse lease context buffer and return oplock/epoch info */
	__u8 (*parse_lease_buf)(void *, unsigned int *);
	int (*clone_range)(const unsigned int, struct cifsFileInfo *src_file,
			struct cifsFileInfo *target_file, u64 src_off, u64 len,
			u64 dest_off);
};

struct smb_version_values {
	char		*version_string;
	__u16		protocol_id;
	__u32		req_capabilities;
	__u32		large_lock_type;
	__u32		exclusive_lock_type;
	__u32		shared_lock_type;
	__u32		unlock_lock_type;
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

struct smb_vol {
	char *username;
	char *password;
	char *domainname;
	char *UNC;
	char *iocharset;  /* local code page for mapping to and from Unicode */
	char source_rfc1001_name[RFC1001_NAME_LEN_WITH_NULL]; /* clnt nb name */
	char target_rfc1001_name[RFC1001_NAME_LEN_WITH_NULL]; /* srvr nb name */
	kuid_t cred_uid;
	kuid_t linux_uid;
	kgid_t linux_gid;
	kuid_t backupuid;
	kgid_t backupgid;
	umode_t file_mode;
	umode_t dir_mode;
	enum securityEnum sectype; /* sectype requested via mnt opts */
	bool sign; /* was signing requested via mnt opts? */
	bool retry:1;
	bool intr:1;
	bool setuids:1;
	bool override_uid:1;
	bool override_gid:1;
	bool dynperm:1;
	bool noperm:1;
	bool no_psx_acl:1; /* set if posix acl support should be disabled */
	bool cifs_acl:1;
	bool backupuid_specified; /* mount option  backupuid  is specified */
	bool backupgid_specified; /* mount option  backupgid  is specified */
	bool no_xattr:1;   /* set if xattr (EA) support should be disabled*/
	bool server_ino:1; /* use inode numbers from server ie UniqueId */
	bool direct_io:1;
	bool strict_io:1; /* strict cache behavior */
	bool remap:1;      /* set to remap seven reserved chars in filenames */
	bool posix_paths:1; /* unset to not ask for posix pathnames. */
	bool no_linux_ext:1;
	bool sfu_emul:1;
	bool nullauth:1;   /* attempt to authenticate with null user */
	bool nocase:1;     /* request case insensitive filenames */
	bool nobrl:1;      /* disable sending byte range locks to srv */
	bool mand_lock:1;  /* send mandatory not posix byte range lock reqs */
	bool seal:1;       /* request transport encryption on share */
	bool nodfs:1;      /* Do not request DFS, even if available */
	bool local_lease:1; /* check leases only on local system, not remote */
	bool noblocksnd:1;
	bool noautotune:1;
	bool nostrictsync:1; /* do not force expensive SMBflush on every sync */
	bool fsc:1;	/* enable fscache */
	bool mfsymlinks:1; /* use Minshall+French Symlinks */
	bool multiuser:1;
	bool rwpidforward:1; /* pid forward for read/write operations */
	bool nosharesock;
	unsigned int rsize;
	unsigned int wsize;
	bool sockopt_tcp_nodelay:1;
	unsigned long actimeo; /* attribute cache timeout (jiffies) */
	struct smb_version_operations *ops;
	struct smb_version_values *vals;
	char *prepath;
	struct sockaddr_storage dstaddr; /* destination address */
	struct sockaddr_storage srcaddr; /* allow binding to a local IP */
	struct nls_table *local_nls;
};

#define CIFS_MOUNT_MASK (CIFS_MOUNT_NO_PERM | CIFS_MOUNT_SET_UID | \
			 CIFS_MOUNT_SERVER_INUM | CIFS_MOUNT_DIRECT_IO | \
			 CIFS_MOUNT_NO_XATTR | CIFS_MOUNT_MAP_SPECIAL_CHR | \
			 CIFS_MOUNT_UNX_EMUL | CIFS_MOUNT_NO_BRL | \
			 CIFS_MOUNT_CIFS_ACL | CIFS_MOUNT_OVERR_UID | \
			 CIFS_MOUNT_OVERR_GID | CIFS_MOUNT_DYNPERM | \
			 CIFS_MOUNT_NOPOSIXBRL | CIFS_MOUNT_NOSSYNC | \
			 CIFS_MOUNT_FSCACHE | CIFS_MOUNT_MF_SYMLINKS | \
			 CIFS_MOUNT_MULTIUSER | CIFS_MOUNT_STRICT_IO | \
			 CIFS_MOUNT_CIFS_BACKUPUID | CIFS_MOUNT_CIFS_BACKUPGID)

#define CIFS_MS_MASK (MS_RDONLY | MS_MANDLOCK | MS_NOEXEC | MS_NOSUID | \
		      MS_NODEV | MS_SYNCHRONOUS)

struct cifs_mnt_data {
	struct cifs_sb_info *cifs_sb;
	struct smb_vol *vol;
	int flags;
};

static inline unsigned int
get_rfc1002_length(void *buf)
{
	return be32_to_cpu(*((__be32 *)buf));
}

static inline void
inc_rfc1001_len(void *buf, int count)
{
	be32_add_cpu((__be32 *)buf, count);
}

struct TCP_Server_Info {
	struct list_head tcp_ses_list;
	struct list_head smb_ses_list;
	int srv_count; /* reference counter */
	/* 15 character server name + 0x20 16th byte indicating type = srv */
	char server_RFC1001_name[RFC1001_NAME_LEN_WITH_NULL];
	struct smb_version_operations	*ops;
	struct smb_version_values	*vals;
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
	struct list_head pending_mid_q;
	bool noblocksnd;		/* use blocking sendmsg */
	bool noautotune;		/* do not autotune send buf sizes */
	bool tcp_nodelay;
	int credits;  /* send no more requests at once */
	unsigned int in_flight;  /* number of requests on the wire to server */
	spinlock_t req_lock;  /* protect the two values above */
	struct mutex srv_mutex;
	struct task_struct *tsk;
	char server_GUID[16];
	__u16 sec_mode;
	bool sign; /* is signing enabled on this connection? */
	bool session_estab; /* mark when very first sess is established */
#ifdef CONFIG_CIFS_SMB2
	int echo_credits;  /* echo reserved slots */
	int oplock_credits;  /* oplock break reserved slots */
	bool echoes:1; /* enable echoes */
#endif
	u16 dialect; /* dialect index that server chose */
	bool oplocks:1; /* enable oplocks */
	unsigned int maxReq;	/* Clients should submit no more */
	/* than maxReq distinct unanswered SMBs to the server when using  */
	/* multiplexed reads or writes */
	unsigned int maxBuf;	/* maxBuf specifies the maximum */
	/* message size the server can send or receive for non-raw SMBs */
	/* maxBuf is returned by SMB NegotiateProtocol so maxBuf is only 0 */
	/* when socket is setup (and during reconnect) before NegProt sent */
	unsigned int max_rw;	/* maxRw specifies the maximum */
	/* message size the server can send or receive for */
	/* SMB_COM_WRITE_RAW or SMB_COM_READ_RAW. */
	unsigned int capabilities; /* selective disabling of caps by smb sess */
	int timeAdj;  /* Adjust for difference in server time zone in sec */
	__u64 CurrentMid;         /* multiplex id - rotating counter */
	char cryptkey[CIFS_CRYPTO_KEY_SIZE]; /* used by ntlm, ntlmv2 etc */
	/* 16th byte of RFC1001 workstation name is always null */
	char workstation_RFC1001_name[RFC1001_NAME_LEN_WITH_NULL];
	__u32 sequence_number; /* for signing, protected by srv_mutex */
	struct session_key session_key;
	unsigned long lstrp; /* when we got last response from this server */
	struct cifs_secmech secmech; /* crypto sec mech functs, descriptors */
#define	CIFS_NEGFLAVOR_LANMAN	0	/* wct == 13, LANMAN */
#define	CIFS_NEGFLAVOR_UNENCAP	1	/* wct == 17, but no ext_sec */
#define	CIFS_NEGFLAVOR_EXTENDED	2	/* wct == 17, ext_sec bit set */
	char	negflavor;	/* NEGOTIATE response flavor */
	/* extended security flavors that server supports */
	bool	sec_ntlmssp;		/* supports NTLMSSP */
	bool	sec_kerberosu2u;	/* supports U2U Kerberos */
	bool	sec_kerberos;		/* supports plain Kerberos */
	bool	sec_mskerberos;		/* supports legacy MS Kerberos */
	bool	large_buf;		/* is current buffer large? */
	struct delayed_work	echo; /* echo ping workqueue job */
	struct kvec *iov;	/* reusable kvec array for receives */
	unsigned int nr_iov;	/* number of kvecs in array */
	char	*smallbuf;	/* pointer to current "small" buffer */
	char	*bigbuf;	/* pointer to current "big" buffer */
	unsigned int total_read; /* total amount of data read in this pass */
#ifdef CONFIG_CIFS_FSCACHE
	struct fscache_cookie   *fscache; /* client index cache cookie */
#endif
#ifdef CONFIG_CIFS_STATS2
	atomic_t in_send; /* requests trying to send */
	atomic_t num_waiters;   /* blocked waiting to get in sendrecv */
#endif
#ifdef CONFIG_CIFS_SMB2
	unsigned int	max_read;
	unsigned int	max_write;
#endif /* CONFIG_CIFS_SMB2 */
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
has_credits(struct TCP_Server_Info *server, int *credits)
{
	int num;
	spin_lock(&server->req_lock);
	num = *credits;
	spin_unlock(&server->req_lock);
	return num > 0;
}

static inline void
add_credits(struct TCP_Server_Info *server, const unsigned int add,
	    const int optype)
{
	server->ops->add_credits(server, add, optype);
}

static inline void
set_credits(struct TCP_Server_Info *server, const int val)
{
	server->ops->set_credits(server, val);
}

static inline __u64
get_next_mid64(struct TCP_Server_Info *server)
{
	return server->ops->get_next_mid(server);
}

static inline __le16
get_next_mid(struct TCP_Server_Info *server)
{
	__u16 mid = get_next_mid64(server);
	/*
	 * The value in the SMB header should be little endian for easy
	 * on-the-wire decoding.
	 */
	return cpu_to_le16(mid);
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
 * pages. A 16M write means ~32kb page array with PAGE_CACHE_SIZE == 4096.
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

/*
 * The default wsize is 1M. find_get_pages seems to return a maximum of 256
 * pages in a single call. With PAGE_CACHE_SIZE == 4k, this means we can fill
 * a single wsize request with a single call.
 */
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
 * http://blogs.msdn.com/b/openspecification/archive/2009/04/10/smb-maximum-transmit-buffer-size-and-performance-tuning.aspx
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

/*
 * Session structure.  One of these for each uid session with a particular host
 */
struct cifs_ses {
	struct list_head smb_ses_list;
	struct list_head tcon_list;
	struct mutex session_mutex;
	struct TCP_Server_Info *server;	/* pointer to server info */
	int ses_count;		/* reference counter */
	enum statusEnum status;
	unsigned overrideSecFlg;  /* if non-zero override global sec flags */
	__u16 ipc_tid;		/* special tid for connection to IPC share */
	char *serverOS;		/* name of operating system underlying server */
	char *serverNOS;	/* name of network operating system of server */
	char *serverDomain;	/* security realm of server */
	__u64 Suid;		/* remote smb uid  */
	kuid_t linux_uid;	/* overriding owner of files on the mount */
	kuid_t cred_uid;	/* owner of credentials */
	unsigned int capabilities;
	char serverName[SERVER_NAME_LEN_WITH_NULL * 2];	/* BB make bigger for
				TCP names - will ipv6 and sctp addresses fit? */
	char *user_name;	/* must not be null except during init of sess
				   and after mount option parsing we fill it */
	char *domainName;
	char *password;
	struct session_key auth_key;
	struct ntlmssp_auth *ntlmssp; /* ciphertext, flags, server challenge */
	enum securityEnum sectype; /* what security flavor was specified? */
	bool sign;		/* is signing required? */
	bool need_reconnect:1; /* connection reset, uid now invalid */
#ifdef CONFIG_CIFS_SMB2
	__u16 session_flags;
	char smb3signingkey[SMB3_SIGN_KEY_SIZE]; /* for signing smb3 packets */
#endif /* CONFIG_CIFS_SMB2 */
};

static inline bool
cap_unix(struct cifs_ses *ses)
{
	return ses->server->vals->cap_unix & ses->capabilities;
}

/*
 * there is one of these for each connection to a resource on a particular
 * session
 */
struct cifs_tcon {
	struct list_head tcon_list;
	int tc_count;
	struct list_head openFileList;
	struct cifs_ses *ses;	/* pointer to session associated with */
	char treeName[MAX_TREE_SIZE + 1]; /* UNC name of resource in ASCII */
	char *nativeFileSystem;
	char *password;		/* for share-level security */
	__u32 tid;		/* The 4 byte tree id */
	__u16 Flags;		/* optional support bits */
	enum statusEnum tidStatus;
#ifdef CONFIG_CIFS_STATS
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
#ifdef CONFIG_CIFS_SMB2
		struct {
			atomic_t smb2_com_sent[NUMBER_OF_SMB2_COMMANDS];
			atomic_t smb2_com_failed[NUMBER_OF_SMB2_COMMANDS];
		} smb2_stats;
#endif /* CONFIG_CIFS_SMB2 */
	} stats;
#ifdef CONFIG_CIFS_STATS2
	unsigned long long time_writes;
	unsigned long long time_reads;
	unsigned long long time_opens;
	unsigned long long time_deletes;
	unsigned long long time_closes;
	unsigned long long time_mkdirs;
	unsigned long long time_rmdirs;
	unsigned long long time_renames;
	unsigned long long time_t2renames;
	unsigned long long time_ffirst;
	unsigned long long time_fnext;
	unsigned long long time_fclose;
#endif /* CONFIG_CIFS_STATS2 */
	__u64    bytes_read;
	__u64    bytes_written;
	spinlock_t stat_lock;
#endif /* CONFIG_CIFS_STATS */
	FILE_SYSTEM_DEVICE_INFO fsDevInfo;
	FILE_SYSTEM_ATTRIBUTE_INFO fsAttrInfo; /* ok if fs name truncated */
	FILE_SYSTEM_UNIX_INFO fsUnixInfo;
	bool ipc:1;		/* set if connection to IPC$ eg for RPC/PIPES */
	bool retry:1;
	bool nocase:1;
	bool seal:1;      /* transport encryption for this mounted share */
	bool unix_ext:1;  /* if false disable Linux extensions to CIFS protocol
				for this mount even if server would support */
	bool local_lease:1; /* check leases (only) on local system not remote */
	bool broken_posix_open; /* e.g. Samba server versions < 3.3.2, 3.2.9 */
	bool need_reconnect:1; /* connection reset, tid now invalid */
#ifdef CONFIG_CIFS_SMB2
	bool print:1;		/* set if connection to printer share */
	bool bad_network_name:1; /* set if ret status STATUS_BAD_NETWORK_NAME */
	__le32 capabilities;
	__u32 share_flags;
	__u32 maximal_access;
	__u32 vol_serial_number;
	__le64 vol_create_time;
	__u32 ss_flags;		/* sector size flags */
	__u32 perf_sector_size; /* best sector size for perf */
	__u32 max_chunks;
	__u32 max_bytes_chunk;
	__u32 max_bytes_copy;
#endif /* CONFIG_CIFS_SMB2 */
#ifdef CONFIG_CIFS_FSCACHE
	u64 resource_id;		/* server resource id */
	struct fscache_cookie *fscache;	/* cookie for share */
#endif
	struct list_head pending_opens;	/* list of incomplete opens */
	/* BB add field for back pointer to sb struct(s)? */
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

static inline struct cifs_tcon *
tlink_tcon(struct tcon_link *tlink)
{
	return tlink->tl_tcon;
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
	__u32 type;
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

struct cifs_open_parms {
	struct cifs_tcon *tcon;
	struct cifs_sb_info *cifs_sb;
	int disposition;
	int desired_access;
	int create_options;
	const char *path;
	struct cifs_fid *fid;
	bool reconnect:1;
};

struct cifs_fid {
	__u16 netfid;
#ifdef CONFIG_CIFS_SMB2
	__u64 persistent_fid;	/* persist file id for smb2 */
	__u64 volatile_fid;	/* volatile file id for smb2 */
	__u8 lease_key[SMB2_LEASE_KEY_SIZE];	/* lease key for smb2 */
#endif
	struct cifs_pending_open *pending_open;
	unsigned int epoch;
	bool purge_cache;
};

struct cifs_fid_locks {
	struct list_head llist;
	struct cifsFileInfo *cfile;	/* fid that owns locks */
	struct list_head locks;		/* locks held by fid above */
};

struct cifsFileInfo {
	struct list_head tlist;	/* pointer to next fid owned by tcon */
	struct list_head flist;	/* next fid (file instance) for this inode */
	struct cifs_fid_locks *llist;	/* brlocks held by this fid */
	kuid_t uid;		/* allows finding which FileInfo structure */
	__u32 pid;		/* process id who opened file */
	struct cifs_fid fid;	/* file id from remote */
	/* BB add lock scope info here if needed */ ;
	/* lock scope id (0 if none) */
	struct dentry *dentry;
	unsigned int f_flags;
	struct tcon_link *tlink;
	bool invalidHandle:1;	/* file closed via session abend */
	bool oplock_break_cancelled:1;
	int count;		/* refcount protected by cifs_file_list_lock */
	struct mutex fh_mutex; /* prevents reopen race after dead ses*/
	struct cifs_search_info srch_inf;
	struct work_struct oplock_break; /* work for oplock breaks */
};

struct cifs_io_parms {
	__u16 netfid;
#ifdef CONFIG_CIFS_SMB2
	__u64 persistent_fid;	/* persist file id for smb2 */
	__u64 volatile_fid;	/* volatile file id for smb2 */
#endif
	__u32 pid;
	__u64 offset;
	unsigned int length;
	struct cifs_tcon *tcon;
};

struct cifs_readdata;

/* asynchronous read support */
struct cifs_readdata {
	struct kref			refcount;
	struct list_head		list;
	struct completion		done;
	struct cifsFileInfo		*cfile;
	struct address_space		*mapping;
	__u64				offset;
	unsigned int			bytes;
	pid_t				pid;
	int				result;
	struct work_struct		work;
	int (*read_into_pages)(struct TCP_Server_Info *server,
				struct cifs_readdata *rdata,
				unsigned int len);
	struct kvec			iov;
	unsigned int			pagesz;
	unsigned int			tailsz;
	unsigned int			nr_pages;
	struct page			*pages[];
};

struct cifs_writedata;

/* asynchronous write support */
struct cifs_writedata {
	struct kref			refcount;
	struct list_head		list;
	struct completion		done;
	enum writeback_sync_modes	sync_mode;
	struct work_struct		work;
	struct cifsFileInfo		*cfile;
	__u64				offset;
	pid_t				pid;
	unsigned int			bytes;
	int				result;
	unsigned int			pagesz;
	unsigned int			tailsz;
	unsigned int			nr_pages;
	struct page			*pages[1];
};

/*
 * Take a reference on the file private data. Must be called with
 * cifs_file_list_lock held.
 */
static inline void
cifsFileInfo_get_locked(struct cifsFileInfo *cifs_file)
{
	++cifs_file->count;
}

struct cifsFileInfo *cifsFileInfo_get(struct cifsFileInfo *cifs_file);
void cifsFileInfo_put(struct cifsFileInfo *cifs_file);

#define CIFS_CACHE_READ_FLG	1
#define CIFS_CACHE_HANDLE_FLG	2
#define CIFS_CACHE_RH_FLG	(CIFS_CACHE_READ_FLG | CIFS_CACHE_HANDLE_FLG)
#define CIFS_CACHE_WRITE_FLG	4
#define CIFS_CACHE_RW_FLG	(CIFS_CACHE_READ_FLG | CIFS_CACHE_WRITE_FLG)
#define CIFS_CACHE_RHW_FLG	(CIFS_CACHE_RW_FLG | CIFS_CACHE_HANDLE_FLG)

#define CIFS_CACHE_READ(cinode) (cinode->oplock & CIFS_CACHE_READ_FLG)
#define CIFS_CACHE_HANDLE(cinode) (cinode->oplock & CIFS_CACHE_HANDLE_FLG)
#define CIFS_CACHE_WRITE(cinode) (cinode->oplock & CIFS_CACHE_WRITE_FLG)

/*
 * One of these for each file inode
 */

struct cifsInodeInfo {
	bool can_cache_brlcks;
	struct list_head llist;	/* locks helb by this inode */
	struct rw_semaphore lock_sem;	/* protect the fields above */
	/* BB add in lists for dirty pages i.e. write caching info for oplock */
	struct list_head openFileList;
	__u32 cifsAttrs; /* e.g. DOS archive bit, sparse, compressed, system */
	unsigned int oplock;		/* oplock/lease level we have */
	unsigned int epoch;		/* used to track lease state changes */
	bool delete_pending;		/* DELETE_ON_CLOSE is set */
	bool invalid_mapping;		/* pagecache is invalid */
	unsigned long time;		/* jiffies of last update of inode */
	u64  server_eof;		/* current file size on server -- protected by i_lock */
	u64  uniqueid;			/* server inode number */
	u64  createtime;		/* creation time on server */
#ifdef CONFIG_CIFS_SMB2
	__u8 lease_key[SMB2_LEASE_KEY_SIZE];	/* lease key for this inode */
#endif
#ifdef CONFIG_CIFS_FSCACHE
	struct fscache_cookie *fscache;
#endif
	struct inode vfs_inode;
};

static inline struct cifsInodeInfo *
CIFS_I(struct inode *inode)
{
	return container_of(inode, struct cifsInodeInfo, vfs_inode);
}

static inline struct cifs_sb_info *
CIFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
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

#ifdef CONFIG_CIFS_STATS
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
#else

#define  cifs_stats_inc(field) do {} while (0)
#define  cifs_stats_bytes_written(tcon, bytes) do {} while (0)
#define  cifs_stats_bytes_read(tcon, bytes) do {} while (0)

#endif


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

/* one of these for every pending CIFS request to the server */
struct mid_q_entry {
	struct list_head qhead;	/* mids waiting on reply from this server */
	struct TCP_Server_Info *server;	/* server corresponding to this mid */
	__u64 mid;		/* multiplex id */
	__u32 pid;		/* process id */
	__u32 sequence_number;  /* for CIFS signing */
	unsigned long when_alloc;  /* when mid was created */
#ifdef CONFIG_CIFS_STATS2
	unsigned long when_sent; /* time when smb send finished */
	unsigned long when_received; /* when demux complete (taken off wire) */
#endif
	mid_receive_t *receive; /* call receive callback */
	mid_callback_t *callback; /* call completion callback */
	void *callback_data;	  /* general purpose pointer for callback */
	void *resp_buf;		/* pointer to received SMB header */
	int mid_state;	/* wish this were enum but can not pass to wait_event */
	__le16 command;		/* smb command code */
	bool large_buf:1;	/* if valid response, is pointer to large buf */
	bool multiRsp:1;	/* multiple trans2 responses for one request  */
	bool multiEnd:1;	/* both received */
};

/*	Make code in transport.c a little cleaner by moving
	update of optional stats into function below */
#ifdef CONFIG_CIFS_STATS2

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

static inline void cifs_save_when_sent(struct mid_q_entry *mid)
{
	mid->when_sent = jiffies;
}
#else
static inline void cifs_in_send_inc(struct TCP_Server_Info *server)
{
}
static inline void cifs_in_send_dec(struct TCP_Server_Info *server)
{
}

static inline void cifs_num_waiters_inc(struct TCP_Server_Info *server)
{
}

static inline void cifs_num_waiters_dec(struct TCP_Server_Info *server)
{
}

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
};

/*
 * common struct for holding inode info when searching for or updating an
 * inode with new info
 */

#define CIFS_FATTR_DFS_REFERRAL		0x1
#define CIFS_FATTR_DELETE_PENDING	0x2
#define CIFS_FATTR_NEED_REVAL		0x4
#define CIFS_FATTR_INO_COLLISION	0x8
#define CIFS_FATTR_UNKNOWN_NLINK	0x10

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
	struct timespec	cf_atime;
	struct timespec	cf_mtime;
	struct timespec	cf_ctime;
};

static inline void free_dfs_info_param(struct dfs_info3_param *param)
{
	if (param) {
		kfree(param->path_name);
		kfree(param->node_name);
		kfree(param);
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

#define   MID_FREE 0
#define   MID_REQUEST_ALLOCATED 1
#define   MID_REQUEST_SUBMITTED 2
#define   MID_RESPONSE_RECEIVED 4
#define   MID_RETRY_NEEDED      8 /* session closed while this request out */
#define   MID_RESPONSE_MALFORMED 0x10
#define   MID_SHUTDOWN		 0x20

/* Types of response buffer returned from SendReceive2 */
#define   CIFS_NO_BUFFER        0    /* Response buffer not returned */
#define   CIFS_SMALL_BUFFER     1
#define   CIFS_LARGE_BUFFER     2
#define   CIFS_IOVEC            4    /* array of response buffers */

/* Type of Request to SendReceive2 */
#define   CIFS_BLOCKING_OP      1    /* operation can block */
#define   CIFS_ASYNC_OP         2    /* do not wait for response */
#define   CIFS_TIMEOUT_MASK 0x003    /* only one of above set in req */
#define   CIFS_LOG_ERROR    0x010    /* log NT STATUS if non-zero */
#define   CIFS_LARGE_BUF_OP 0x020    /* large request buffer */
#define   CIFS_NO_RESP      0x040    /* no response buffer required */

/* Type of request operation */
#define   CIFS_ECHO_OP      0x080    /* echo request */
#define   CIFS_OBREAK_OP   0x0100    /* oplock break request */
#define   CIFS_NEG_OP      0x0200    /* negotiate request */
#define   CIFS_OP_MASK     0x0380    /* mask request type */

/* Security Flags: indicate type of session setup needed */
#define   CIFSSEC_MAY_SIGN	0x00001
#define   CIFSSEC_MAY_NTLM	0x00002
#define   CIFSSEC_MAY_NTLMV2	0x00004
#define   CIFSSEC_MAY_KRB5	0x00008
#ifdef CONFIG_CIFS_WEAK_PW_HASH
#define   CIFSSEC_MAY_LANMAN	0x00010
#define   CIFSSEC_MAY_PLNTXT	0x00020
#else
#define   CIFSSEC_MAY_LANMAN    0
#define   CIFSSEC_MAY_PLNTXT    0
#endif /* weak passwords */
#define   CIFSSEC_MAY_SEAL	0x00040 /* not supported yet */
#define   CIFSSEC_MAY_NTLMSSP	0x00080 /* raw ntlmssp with ntlmv2 */

#define   CIFSSEC_MUST_SIGN	0x01001
/* note that only one of the following can be set so the
result of setting MUST flags more than once will be to
require use of the stronger protocol */
#define   CIFSSEC_MUST_NTLM	0x02002
#define   CIFSSEC_MUST_NTLMV2	0x04004
#define   CIFSSEC_MUST_KRB5	0x08008
#ifdef CONFIG_CIFS_WEAK_PW_HASH
#define   CIFSSEC_MUST_LANMAN	0x10010
#define   CIFSSEC_MUST_PLNTXT	0x20020
#ifdef CONFIG_CIFS_UPCALL
#define   CIFSSEC_MASK          0xBF0BF /* allows weak security but also krb5 */
#else
#define   CIFSSEC_MASK          0xB70B7 /* current flags supported if weak */
#endif /* UPCALL */
#else /* do not allow weak pw hash */
#define   CIFSSEC_MUST_LANMAN	0
#define   CIFSSEC_MUST_PLNTXT	0
#ifdef CONFIG_CIFS_UPCALL
#define   CIFSSEC_MASK          0x8F08F /* flags supported if no weak allowed */
#else
#define	  CIFSSEC_MASK          0x87087 /* flags supported if no weak allowed */
#endif /* UPCALL */
#endif /* WEAK_PW_HASH */
#define   CIFSSEC_MUST_SEAL	0x40040 /* not supported yet */
#define   CIFSSEC_MUST_NTLMSSP	0x80080 /* raw ntlmssp with ntlmv2 */

#define   CIFSSEC_DEF (CIFSSEC_MAY_SIGN | CIFSSEC_MAY_NTLMV2 | CIFSSEC_MAY_NTLMSSP)
#define   CIFSSEC_MAX (CIFSSEC_MUST_SIGN | CIFSSEC_MUST_NTLMV2)
#define   CIFSSEC_AUTH_MASK (CIFSSEC_MAY_NTLM | CIFSSEC_MAY_NTLMV2 | CIFSSEC_MAY_LANMAN | CIFSSEC_MAY_PLNTXT | CIFSSEC_MAY_KRB5 | CIFSSEC_MAY_NTLMSSP)
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
 *  Locking notes.  All updates to global variables and lists should be
 *                  protected by spinlocks or semaphores.
 *
 *  Spinlocks
 *  ---------
 *  GlobalMid_Lock protects:
 *	list operations on pending_mid_q and oplockQ
 *      updates to XID counters, multiplex id  and SMB sequence numbers
 *  cifs_file_list_lock protects:
 *	list operations on tcp and SMB session lists and tCon lists
 *  f_owner.lock protects certain per file struct operations
 *  mapping->page_lock protects certain per page operations
 *
 *  Semaphores
 *  ----------
 *  sesSem     operations on smb session
 *  tconSem    operations on tree connection
 *  fh_sem      file handle reconnection operations
 *
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
GLOBAL_EXTERN struct list_head		cifs_tcp_ses_list;

/*
 * This lock protects the cifs_tcp_ses_list, the list of smb sessions per
 * tcp session, and the list of tcon's per smb session. It also protects
 * the reference counters for the server, smb session, and tcon. Finally,
 * changes to the tcon->tidStatus should be done while holding this lock.
 */
GLOBAL_EXTERN spinlock_t		cifs_tcp_ses_lock;

/*
 * This lock protects the cifs_file->llist and cifs_file->flist
 * list operations, and updates to some flags (cifs_file->invalidHandle)
 * It will be moved to either use the tcon->stat_lock or equivalent later.
 * If cifs_tcp_ses_lock and the lock below are both needed to be held, then
 * the cifs_tcp_ses_lock must be grabbed first and released last.
 */
GLOBAL_EXTERN spinlock_t	cifs_file_list_lock;

#ifdef CONFIG_CIFS_DNOTIFY_EXPERIMENTAL /* unused temporarily */
/* Outstanding dir notify requests */
GLOBAL_EXTERN struct list_head GlobalDnotifyReqList;
/* DirNotify response queue */
GLOBAL_EXTERN struct list_head GlobalDnotifyRsp_Q;
#endif /* was needed for dnotify, and will be needed for inotify when VFS fix */

/*
 * Global transaction id (XID) information
 */
GLOBAL_EXTERN unsigned int GlobalCurrentXid;	/* protected by GlobalMid_Sem */
GLOBAL_EXTERN unsigned int GlobalTotalActiveXid; /* prot by GlobalMid_Sem */
GLOBAL_EXTERN unsigned int GlobalMaxActiveXid;	/* prot by GlobalMid_Sem */
GLOBAL_EXTERN spinlock_t GlobalMid_Lock;  /* protects above & list operations */
					  /* on midQ entries */
/*
 *  Global counters, updated atomically
 */
GLOBAL_EXTERN atomic_t sesInfoAllocCount;
GLOBAL_EXTERN atomic_t tconInfoAllocCount;
GLOBAL_EXTERN atomic_t tcpSesAllocCount;
GLOBAL_EXTERN atomic_t tcpSesReconnectCount;
GLOBAL_EXTERN atomic_t tconInfoReconnectCount;

/* Various Debug counters */
GLOBAL_EXTERN atomic_t bufAllocCount;    /* current number allocated  */
#ifdef CONFIG_CIFS_STATS2
GLOBAL_EXTERN atomic_t totBufAllocCount; /* total allocated over all time */
GLOBAL_EXTERN atomic_t totSmBufAllocCount;
#endif
GLOBAL_EXTERN atomic_t smBufAllocCount;
GLOBAL_EXTERN atomic_t midCount;

/* Misc globals */
GLOBAL_EXTERN bool enable_oplocks; /* enable or disable oplocks */
GLOBAL_EXTERN unsigned int lookupCacheEnabled;
GLOBAL_EXTERN unsigned int global_secflags;	/* if on, session setup sent
				with more secure ntlmssp2 challenge/resp */
GLOBAL_EXTERN unsigned int sign_CIFS_PDUs;  /* enable smb packet signing */
GLOBAL_EXTERN unsigned int linuxExtEnabled;/*enable Linux/Unix CIFS extensions*/
GLOBAL_EXTERN unsigned int CIFSMaxBufSize;  /* max size not including hdr */
GLOBAL_EXTERN unsigned int cifs_min_rcv;    /* min size of big ntwrk buf pool */
GLOBAL_EXTERN unsigned int cifs_min_small;  /* min size of small buf pool */
GLOBAL_EXTERN unsigned int cifs_max_pending; /* MAX requests at once to server*/

#ifdef CONFIG_CIFS_ACL
GLOBAL_EXTERN struct rb_root uidtree;
GLOBAL_EXTERN struct rb_root gidtree;
GLOBAL_EXTERN spinlock_t siduidlock;
GLOBAL_EXTERN spinlock_t sidgidlock;
GLOBAL_EXTERN struct rb_root siduidtree;
GLOBAL_EXTERN struct rb_root sidgidtree;
GLOBAL_EXTERN spinlock_t uidsidlock;
GLOBAL_EXTERN spinlock_t gidsidlock;
#endif /* CONFIG_CIFS_ACL */

void cifs_oplock_break(struct work_struct *work);

extern const struct slow_work_ops cifs_oplock_break_ops;
extern struct workqueue_struct *cifsiod_wq;

extern mempool_t *cifs_mid_poolp;

/* Operations for different SMB versions */
#define SMB1_VERSION_STRING	"1.0"
extern struct smb_version_operations smb1_operations;
extern struct smb_version_values smb1_values;
#define SMB20_VERSION_STRING	"2.0"
extern struct smb_version_operations smb20_operations;
extern struct smb_version_values smb20_values;
#define SMB21_VERSION_STRING	"2.1"
extern struct smb_version_operations smb21_operations;
extern struct smb_version_values smb21_values;
#define SMB30_VERSION_STRING	"3.0"
extern struct smb_version_operations smb30_operations;
extern struct smb_version_values smb30_values;
#define SMB302_VERSION_STRING	"3.02"
/*extern struct smb_version_operations smb302_operations;*/ /* not needed yet */
extern struct smb_version_values smb302_values;
#endif	/* _CIFS_GLOB_H */
