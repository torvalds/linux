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

/*
 * The sizes of various internal tables and strings
 */
#define MAX_UID_INFO 16
#define MAX_SES_INFO 2
#define MAX_TCON_INFO 4

#define MAX_TREE_SIZE (2 + MAX_SERVER_SIZE + 1 + MAX_SHARE_SIZE + 1)
#define MAX_SERVER_SIZE 15
#define MAX_SHARE_SIZE 80
#define MAX_USERNAME_SIZE 256	/* reasonable maximum for current servers */
#define MAX_PASSWORD_SIZE 512	/* max for windows seems to be 256 wide chars */

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
	LANMAN = 0,			/* Legacy LANMAN auth */
	NTLM,			/* Legacy NTLM012 auth with NTLM hash */
	NTLMv2,			/* Legacy NTLM auth with NTLMv2 hash */
	RawNTLMSSP,		/* NTLMSSP without SPNEGO, NTLMv2 hash */
/*	NTLMSSP, */ /* can use rawNTLMSSP instead of NTLMSSP via SPNEGO */
	Kerberos,		/* Kerberos via SPNEGO */
};

enum protocolEnum {
	TCP = 0,
	SCTP
	/* Netbios frames protocol not supported at this time */
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
	struct sdesc *sdeschmacmd5;  /* ctxt to generate ntlmv2 hash, CR1 */
	struct sdesc *sdescmd5; /* ctxt to generate cifs/smb signature */
};

/* per smb session structure/fields */
struct ntlmssp_auth {
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

enum smb_version {
	Smb_1 = 1,
	Smb_21,
};

struct mid_q_entry;
struct TCP_Server_Info;
struct cifsFileInfo;
struct cifs_ses;
struct cifs_tcon;
struct dfs_info3_param;

struct smb_version_operations {
	int (*send_cancel)(struct TCP_Server_Info *, void *,
			   struct mid_q_entry *);
	bool (*compare_fids)(struct cifsFileInfo *, struct cifsFileInfo *);
	/* setup request: allocate mid, sign message */
	int (*setup_request)(struct cifs_ses *, struct kvec *, unsigned int,
			     struct mid_q_entry **);
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
};

struct smb_version_values {
	char		*version_string;
	__u32		large_lock_type;
	__u32		exclusive_lock_type;
	__u32		shared_lock_type;
	__u32		unlock_lock_type;
	size_t		header_size;
	size_t		max_header_size;
	size_t		read_rsp_size;
	__le16		lock_cmd;
};

#define HEADER_SIZE(server) (server->vals->header_size)
#define MAX_HEADER_SIZE(server) (server->vals->max_header_size)

struct smb_vol {
	char *username;
	char *password;
	char *domainname;
	char *UNC;
	char *UNCip;
	char *iocharset;  /* local code page for mapping to and from Unicode */
	char source_rfc1001_name[RFC1001_NAME_LEN_WITH_NULL]; /* clnt nb name */
	char target_rfc1001_name[RFC1001_NAME_LEN_WITH_NULL]; /* srvr nb name */
	uid_t cred_uid;
	uid_t linux_uid;
	gid_t linux_gid;
	uid_t backupuid;
	gid_t backupgid;
	umode_t file_mode;
	umode_t dir_mode;
	unsigned secFlg;
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
	unsigned int rsize;
	unsigned int wsize;
	bool sockopt_tcp_nodelay:1;
	unsigned short int port;
	unsigned long actimeo; /* attribute cache timeout (jiffies) */
	struct smb_version_operations *ops;
	struct smb_version_values *vals;
	char *prepath;
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
	bool session_estab; /* mark when very first sess is established */
#ifdef CONFIG_CIFS_SMB2
	int echo_credits;  /* echo reserved slots */
	int oplock_credits;  /* oplock break reserved slots */
	bool echoes:1; /* enable echoes */
#endif
	u16 dialect; /* dialect index that server chose */
	enum securityEnum secType;
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
	unsigned int max_vcs;	/* maximum number of smb sessions, at least
				   those that can be specified uniquely with
				   vcnumbers */
	int capabilities; /* allow selective disabling of caps by smb sess */
	int timeAdj;  /* Adjust for difference in server time zone in sec */
	__u64 CurrentMid;         /* multiplex id - rotating counter */
	char cryptkey[CIFS_CRYPTO_KEY_SIZE]; /* used by ntlm, ntlmv2 etc */
	/* 16th byte of RFC1001 workstation name is always null */
	char workstation_RFC1001_name[RFC1001_NAME_LEN_WITH_NULL];
	__u32 sequence_number; /* for signing, protected by srv_mutex */
	struct session_key session_key;
	unsigned long lstrp; /* when we got last response from this server */
	struct cifs_secmech secmech; /* crypto sec mech functs, descriptors */
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
get_next_mid(struct TCP_Server_Info *server)
{
	return server->ops->get_next_mid(server);
}

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
	__u16 flags;
	__u16 vcnum;
	char *serverOS;		/* name of operating system underlying server */
	char *serverNOS;	/* name of network operating system of server */
	char *serverDomain;	/* security realm of server */
	__u64 Suid;		/* remote smb uid  */
	uid_t linux_uid;        /* overriding owner of files on the mount */
	uid_t cred_uid;		/* owner of credentials */
	int capabilities;
	char serverName[SERVER_NAME_LEN_WITH_NULL * 2];	/* BB make bigger for
				TCP names - will ipv6 and sctp addresses fit? */
	char *user_name;	/* must not be null except during init of sess
				   and after mount option parsing we fill it */
	char *domainName;
	char *password;
	struct session_key auth_key;
	struct ntlmssp_auth *ntlmssp; /* ciphertext, flags, server challenge */
	bool need_reconnect:1; /* connection reset, uid now invalid */
#ifdef CONFIG_CIFS_SMB2
	__u16 session_flags;
#endif /* CONFIG_CIFS_SMB2 */
};
/* no more than one of the following three session flags may be set */
#define CIFS_SES_NT4 1
#define CIFS_SES_OS2 2
#define CIFS_SES_W9X 4
/* following flag is set for old servers such as OS2 (and Win95?)
   which do not negotiate NTLM or POSIX dialects, but instead
   negotiate one of the older LANMAN dialects */
#define CIFS_SES_LANMAN 8
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
	__u32 capabilities;
	__u32 share_flags;
	__u32 maximal_access;
	__u32 vol_serial_number;
	__le64 vol_create_time;
#endif /* CONFIG_CIFS_SMB2 */
#ifdef CONFIG_CIFS_FSCACHE
	u64 resource_id;		/* server resource id */
	struct fscache_cookie *fscache;	/* cookie for share */
#endif
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
	uid_t			tl_uid;
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

struct cifsFileInfo {
	struct list_head tlist;	/* pointer to next fid owned by tcon */
	struct list_head flist;	/* next fid (file instance) for this inode */
	struct list_head llist;	/*
				 * brlocks held by this fid, protected by
				 * lock_mutex from cifsInodeInfo structure
				 */
	unsigned int uid;	/* allows finding which FileInfo structure */
	__u32 pid;		/* process id who opened file */
	__u16 netfid;		/* file id from remote */
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
	__u32 pid;
	__u64 offset;
	unsigned int length;
	struct cifs_tcon *tcon;
};

/*
 * Take a reference on the file private data. Must be called with
 * cifs_file_list_lock held.
 */
static inline
struct cifsFileInfo *cifsFileInfo_get(struct cifsFileInfo *cifs_file)
{
	++cifs_file->count;
	return cifs_file;
}

void cifsFileInfo_put(struct cifsFileInfo *cifs_file);

/*
 * One of these for each file inode
 */

struct cifsInodeInfo {
	bool can_cache_brlcks;
	struct mutex lock_mutex;	/*
					 * protect the field above and llist
					 * from every cifsFileInfo structure
					 * from openFileList
					 */
	/* BB add in lists for dirty pages i.e. write caching info for oplock */
	struct list_head openFileList;
	__u32 cifsAttrs; /* e.g. DOS archive bit, sparse, compressed, system */
	bool clientCanCacheRead;	/* read oplock */
	bool clientCanCacheAll;		/* read and writebehind oplock */
	bool delete_pending;		/* DELETE_ON_CLOSE is set */
	bool invalid_mapping;		/* pagecache is invalid */
	unsigned long time;		/* jiffies of last update of inode */
	u64  server_eof;		/* current file size on server -- protected by i_lock */
	u64  uniqueid;			/* server inode number */
	u64  createtime;		/* creation time on server */
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
	int i;
	char old_delim;

	if (path == NULL)
		return;

	if (delim == '/')
		old_delim = '\\';
	else
		old_delim = '/';

	for (i = 0; path[i] != '\0'; i++) {
		if (path[i] == old_delim)
			path[i] = delim;
	}
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

struct cifs_fattr {
	u32		cf_flags;
	u32		cf_cifsattrs;
	u64		cf_uniqueid;
	u64		cf_eof;
	u64		cf_bytes;
	u64		cf_createtime;
	uid_t		cf_uid;
	gid_t		cf_gid;
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

#define   CIFSSEC_DEF (CIFSSEC_MAY_SIGN | CIFSSEC_MAY_NTLM | CIFSSEC_MAY_NTLMV2)
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
#define SMB21_VERSION_STRING	"2.1"
extern struct smb_version_operations smb21_operations;
extern struct smb_version_values smb21_values;
#endif	/* _CIFS_GLOB_H */
