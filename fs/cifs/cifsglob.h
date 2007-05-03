/*
 *   fs/cifs/cifsglob.h
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2006
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
#include <linux/in.h>
#include <linux/in6.h>
#include "cifs_fs_sb.h"
/*
 * The sizes of various internal tables and strings
 */
#define MAX_UID_INFO 16
#define MAX_SES_INFO 2
#define MAX_TCON_INFO 4

#define MAX_TREE_SIZE 2 + MAX_SERVER_SIZE + 1 + MAX_SHARE_SIZE + 1
#define MAX_SERVER_SIZE 15
#define MAX_SHARE_SIZE  64	/* used to be 20 - this should still be enough */
#define MAX_USERNAME_SIZE 32	/* 32 is to allow for 15 char names + null
				   termination then *2 for unicode versions */
#define MAX_PASSWORD_SIZE 16

#define CIFS_MIN_RCV_POOL 4

/*
 * MAX_REQ is the maximum number of requests that WE will send
 * on one socket concurently. It also matches the most common
 * value of max multiplex returned by servers.  We may 
 * eventually want to use the negotiated value (in case
 * future servers can handle more) when we are more confident that
 * we will not have problems oveloading the socket with pending
 * write data.
 */
#define CIFS_MAX_REQ 50 

#define SERVER_NAME_LENGTH 15
#define SERVER_NAME_LEN_WITH_NULL     (SERVER_NAME_LENGTH + 1)

/* used to define string lengths for reversing unicode strings */
/*         (256+1)*2 = 514                                     */
/*           (max path length + 1 for null) * 2 for unicode    */
#define MAX_NAME 514

#include "cifspdu.h"

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef XATTR_DOS_ATTRIB
#define XATTR_DOS_ATTRIB "user.DOSATTRIB"
#endif

/*
 * This information is kept on every Server we know about.
 *
 * Some things to note:
 *
 */
#define SERVER_NAME_LEN_WITH_NULL	(SERVER_NAME_LENGTH + 1)

/*
 * CIFS vfs client Status information (based on what we know.)
 */

 /* associated with each tcp and smb session */
enum statusEnum {
	CifsNew = 0,
	CifsGood,
	CifsExiting,
	CifsNeedReconnect
};

enum securityEnum {
	LANMAN = 0,             /* Legacy LANMAN auth */
	NTLM,			/* Legacy NTLM012 auth with NTLM hash */
	NTLMv2,			/* Legacy NTLM auth with NTLMv2 hash */
	RawNTLMSSP,		/* NTLMSSP without SPNEGO */
	NTLMSSP,		/* NTLMSSP via SPNEGO */
	Kerberos		/* Kerberos via SPNEGO */
};

enum protocolEnum {
	IPV4 = 0,
	IPV6,
	SCTP
	/* Netbios frames protocol not supported at this time */
};

/*
 *****************************************************************
 * Except the CIFS PDUs themselves all the
 * globally interesting structs should go here
 *****************************************************************
 */

struct TCP_Server_Info {
	/* 15 character server name + 0x20 16th byte indicating type = srv */
	char server_RFC1001_name[SERVER_NAME_LEN_WITH_NULL];
	char unicode_server_Name[SERVER_NAME_LEN_WITH_NULL * 2];
	struct socket *ssocket;
	union {
		struct sockaddr_in sockAddr;
		struct sockaddr_in6 sockAddr6;
	} addr;
	wait_queue_head_t response_q; 
	wait_queue_head_t request_q; /* if more than maxmpx to srvr must block*/
	struct list_head pending_mid_q;
	void *Server_NlsInfo;	/* BB - placeholder for future NLS info  */
	unsigned short server_codepage;	/* codepage for the server    */
	unsigned long ip_address;	/* IP addr for the server if known */
	enum protocolEnum protocolType;	
	char versionMajor;
	char versionMinor;
	unsigned svlocal:1;	/* local server or remote */
	atomic_t socketUseCount; /* number of open cifs sessions on socket */
	atomic_t inFlight;  /* number of requests on the wire to server */
#ifdef CONFIG_CIFS_STATS2
	atomic_t inSend; /* requests trying to send */
	atomic_t num_waiters;   /* blocked waiting to get in sendrecv */
#endif
	enum statusEnum tcpStatus; /* what we think the status is */
	struct semaphore tcpSem;
	struct task_struct *tsk;
	char server_GUID[16];
	char secMode;
	enum securityEnum secType;
	unsigned int maxReq;	/* Clients should submit no more */
	/* than maxReq distinct unanswered SMBs to the server when using  */
	/* multiplexed reads or writes */
	unsigned int maxBuf;	/* maxBuf specifies the maximum */
	/* message size the server can send or receive for non-raw SMBs */
	unsigned int maxRw;	/* maxRw specifies the maximum */
	/* message size the server can send or receive for */
	/* SMB_COM_WRITE_RAW or SMB_COM_READ_RAW. */
	char sessid[4];		/* unique token id for this session */
	/* (returned on Negotiate */
	int capabilities; /* allow selective disabling of caps by smb sess */
	int timeAdj;  /* Adjust for difference in server time zone in sec */
	__u16 CurrentMid;         /* multiplex id - rotating counter */
	char cryptKey[CIFS_CRYPTO_KEY_SIZE];
	/* 16th byte of RFC1001 workstation name is always null */
	char workstation_RFC1001_name[SERVER_NAME_LEN_WITH_NULL];
	__u32 sequence_number; /* needed for CIFS PDU signature */
	char mac_signing_key[CIFS_SESS_KEY_SIZE + 16];
	unsigned long lstrp; /* when we got last response from this server */
};

/*
 * The following is our shortcut to user information.  We surface the uid,
 * and name. We always get the password on the fly in case it
 * has changed. We also hang a list of sessions owned by this user off here. 
 */
struct cifsUidInfo {
	struct list_head userList;
	struct list_head sessionList; /* SMB sessions for this user */
	uid_t linux_uid;
	char user[MAX_USERNAME_SIZE + 1];	/* ascii name of user */
	/* BB may need ptr or callback for PAM or WinBind info */
};

/*
 * Session structure.  One of these for each uid session with a particular host
 */
struct cifsSesInfo {
	struct list_head cifsSessionList;
	struct semaphore sesSem;
#if 0
	struct cifsUidInfo *uidInfo;	/* pointer to user info */
#endif
	struct TCP_Server_Info *server;	/* pointer to server info */
	atomic_t inUse; /* # of mounts (tree connections) on this ses */
	enum statusEnum status;
	unsigned overrideSecFlg;  /* if non-zero override global sec flags */
	__u16 ipc_tid;		/* special tid for connection to IPC share */
	__u16 flags;
	char *serverOS;		/* name of operating system underlying server */
	char *serverNOS;	/* name of network operating system of server */
	char *serverDomain;	/* security realm of server */
	int Suid;		/* remote smb uid  */
	uid_t linux_uid;        /* local Linux uid */
	int capabilities;
	char serverName[SERVER_NAME_LEN_WITH_NULL * 2];	/* BB make bigger for 
				TCP names - will ipv6 and sctp addresses fit? */
	char userName[MAX_USERNAME_SIZE + 1];
	char * domainName;
	char * password;
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
struct cifsTconInfo {
	struct list_head cifsConnectionList;
	struct list_head openFileList;
	struct semaphore tconSem;
	struct cifsSesInfo *ses;	/* pointer to session associated with */
	char treeName[MAX_TREE_SIZE + 1]; /* UNC name of resource in ASCII */
	char *nativeFileSystem;
	__u16 tid;		/* The 2 byte tree id */
	__u16 Flags;		/* optional support bits */
	enum statusEnum tidStatus;
	atomic_t useCount;	/* how many explicit/implicit mounts to share */
#ifdef CONFIG_CIFS_STATS
	atomic_t num_smbs_sent;
	atomic_t num_writes;
	atomic_t num_reads;
	atomic_t num_oplock_brks;
	atomic_t num_opens;
	atomic_t num_closes;
	atomic_t num_deletes;
	atomic_t num_mkdirs;
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
	unsigned retry:1;
	unsigned nocase:1;
	/* BB add field for back pointer to sb struct? */
};

/*
 * This info hangs off the cifsFileInfo structure, pointed to by llist.
 * This is used to track byte stream locks on the file
 */
struct cifsLockInfo {
	struct list_head llist;	/* pointer to next cifsLockInfo */
	__u64 offset;
	__u64 length;
	__u8 type;
};

/*
 * One of these for each open instance of a file
 */
struct cifs_search_info {
	loff_t index_of_last_entry;
	__u16 entries_in_buffer;
	__u16 info_level;
	__u32 resume_key;
	char * ntwrk_buf_start;
	char * srch_entries_start;
	char * presume_name;
	unsigned int resume_name_len;
	unsigned endOfSearch:1;
	unsigned emptyDir:1;
	unsigned unicode:1;
	unsigned smallBuf:1; /* so we know which buf_release function to call */
};

struct cifsFileInfo {
	struct list_head tlist;	/* pointer to next fid owned by tcon */
	struct list_head flist;	/* next fid (file instance) for this inode */
	unsigned int uid;	/* allows finding which FileInfo structure */
	__u32 pid;		/* process id who opened file */
	__u16 netfid;		/* file id from remote */
	/* BB add lock scope info here if needed */ ;
	/* lock scope id (0 if none) */
	struct file * pfile; /* needed for writepage */
	struct inode * pInode; /* needed for oplock break */
	struct mutex lock_mutex;
	struct list_head llist; /* list of byte range locks we have. */
	unsigned closePend:1;	/* file is marked to close */
	unsigned invalidHandle:1;  /* file closed via session abend */
	atomic_t wrtPending;   /* handle in use - defer close */
	struct semaphore fh_sem; /* prevents reopen race after dead ses*/
	char * search_resume_name; /* BB removeme BB */
	struct cifs_search_info srch_inf;
};

/*
 * One of these for each file inode
 */

struct cifsInodeInfo {
	struct list_head lockList;
	/* BB add in lists for dirty pages - i.e. write caching info for oplock */
	struct list_head openFileList;
	int write_behind_rc;
	__u32 cifsAttrs; /* e.g. DOS archive bit, sparse, compressed, system */
	atomic_t inUse;	 /* num concurrent users (local openers cifs) of file*/
	unsigned long time;	/* jiffies of last update/check of inode */
	unsigned clientCanCacheRead:1; /* read oplock */
	unsigned clientCanCacheAll:1;  /* read and writebehind oplock */
	unsigned oplockPending:1;
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

#ifdef CONFIG_CIFS_STATS
#define cifs_stats_inc atomic_inc

static inline void cifs_stats_bytes_written(struct cifsTconInfo *tcon,
					    unsigned int bytes)
{
	if (bytes) {
		spin_lock(&tcon->stat_lock);
		tcon->bytes_written += bytes;
		spin_unlock(&tcon->stat_lock);
	}
}

static inline void cifs_stats_bytes_read(struct cifsTconInfo *tcon,
					 unsigned int bytes)
{
	spin_lock(&tcon->stat_lock);
	tcon->bytes_read += bytes;
	spin_unlock(&tcon->stat_lock);
}
#else

#define  cifs_stats_inc(field) do {} while(0)
#define  cifs_stats_bytes_written(tcon, bytes) do {} while(0)
#define  cifs_stats_bytes_read(tcon, bytes) do {} while(0)

#endif

/* one of these for every pending CIFS request to the server */
struct mid_q_entry {
	struct list_head qhead;	/* mids waiting on reply from this server */
	__u16 mid;		/* multiplex id */
	__u16 pid;		/* process id */
	__u32 sequence_number;  /* for CIFS signing */
	unsigned long when_alloc;  /* when mid was created */
#ifdef CONFIG_CIFS_STATS2
	unsigned long when_sent; /* time when smb send finished */
	unsigned long when_received; /* when demux complete (taken off wire) */
#endif
	struct cifsSesInfo *ses;	/* smb was sent to this server */
	struct task_struct *tsk;	/* task waiting for response */
	struct smb_hdr *resp_buf;	/* response buffer */
	int midState;	/* wish this were enum but can not pass to wait_event */
	__u8 command;	/* smb command code */
	unsigned largeBuf:1;    /* if valid response, is pointer to large buf */
	unsigned multiRsp:1;   /* multiple trans2 responses for one request  */
	unsigned multiEnd:1; /* both received */
};

struct oplock_q_entry {
	struct list_head qhead;
	struct inode * pinode;
	struct cifsTconInfo * tcon; 
	__u16 netfid;
};

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
       struct file * pfile;
};

#define   MID_FREE 0
#define   MID_REQUEST_ALLOCATED 1
#define   MID_REQUEST_SUBMITTED 2
#define   MID_RESPONSE_RECEIVED 4
#define   MID_RETRY_NEEDED      8 /* session closed while this request out */
#define   MID_NO_RESP_NEEDED 0x10

/* Types of response buffer returned from SendReceive2 */
#define   CIFS_NO_BUFFER        0    /* Response buffer not returned */
#define   CIFS_SMALL_BUFFER     1
#define   CIFS_LARGE_BUFFER     2
#define   CIFS_IOVEC            4    /* array of response buffers */

/* Security Flags: indicate type of session setup needed */
#define   CIFSSEC_MAY_SIGN	0x00001
#define   CIFSSEC_MAY_NTLM	0x00002
#define   CIFSSEC_MAY_NTLMV2	0x00004
#define   CIFSSEC_MAY_KRB5	0x00008
#ifdef CONFIG_CIFS_WEAK_PW_HASH
#define   CIFSSEC_MAY_LANMAN	0x00010
#define   CIFSSEC_MAY_PLNTXT	0x00020
#endif /* weak passwords */
#define   CIFSSEC_MAY_SEAL	0x00040 /* not supported yet */

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
#define   CIFSSEC_MASK          0x37037 /* current flags supported if weak */
#else	  
#define	  CIFSSEC_MASK          0x07007 /* flags supported if no weak config */
#endif /* WEAK_PW_HASH */
#define   CIFSSEC_MUST_SEAL	0x40040 /* not supported yet */

#define   CIFSSEC_DEF  CIFSSEC_MAY_SIGN | CIFSSEC_MAY_NTLM | CIFSSEC_MAY_NTLMV2
#define   CIFSSEC_MAX  CIFSSEC_MUST_SIGN | CIFSSEC_MUST_NTLMV2
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
 *  GlobalSMBSesLock protects:
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
 * The list of servers that did not respond with NT LM 0.12.
 * This list helps improve performance and eliminate the messages indicating
 * that we had a communications error talking to the server in this list. 
 */
/* Feature not supported */
/* GLOBAL_EXTERN struct servers_not_supported *NotSuppList; */

/*
 * The following is a hash table of all the users we know about.
 */
GLOBAL_EXTERN struct smbUidInfo *GlobalUidList[UID_HASH];

/* GLOBAL_EXTERN struct list_head GlobalServerList; BB not implemented yet */
GLOBAL_EXTERN struct list_head GlobalSMBSessionList;
GLOBAL_EXTERN struct list_head GlobalTreeConnectionList;
GLOBAL_EXTERN rwlock_t GlobalSMBSeslock;  /* protects list inserts on 3 above */

GLOBAL_EXTERN struct list_head GlobalOplock_Q;

/* Outstanding dir notify requests */
GLOBAL_EXTERN struct list_head GlobalDnotifyReqList;
/* DirNotify response queue */
GLOBAL_EXTERN struct list_head GlobalDnotifyRsp_Q;

/*
 * Global transaction id (XID) information
 */
GLOBAL_EXTERN unsigned int GlobalCurrentXid;	/* protected by GlobalMid_Sem */
GLOBAL_EXTERN unsigned int GlobalTotalActiveXid; /* prot by GlobalMid_Sem */
GLOBAL_EXTERN unsigned int GlobalMaxActiveXid;	/* prot by GlobalMid_Sem */
GLOBAL_EXTERN spinlock_t GlobalMid_Lock;  /* protects above & list operations */
					  /* on midQ entries */
GLOBAL_EXTERN char Local_System_Name[15];

/*
 *  Global counters, updated atomically
 */
GLOBAL_EXTERN atomic_t sesInfoAllocCount;
GLOBAL_EXTERN atomic_t tconInfoAllocCount;
GLOBAL_EXTERN atomic_t tcpSesAllocCount;
GLOBAL_EXTERN atomic_t tcpSesReconnectCount;
GLOBAL_EXTERN atomic_t tconInfoReconnectCount;

/* Various Debug counters to remove someday (BB) */
GLOBAL_EXTERN atomic_t bufAllocCount;    /* current number allocated  */
#ifdef CONFIG_CIFS_STATS2
GLOBAL_EXTERN atomic_t totBufAllocCount; /* total allocated over all time */
GLOBAL_EXTERN atomic_t totSmBufAllocCount;
#endif
GLOBAL_EXTERN atomic_t smBufAllocCount;
GLOBAL_EXTERN atomic_t midCount;

/* Misc globals */
GLOBAL_EXTERN unsigned int multiuser_mount; /* if enabled allows new sessions
				to be established on existing mount if we
				have the uid/password or Kerberos credential 
				or equivalent for current user */
GLOBAL_EXTERN unsigned int oplockEnabled;
GLOBAL_EXTERN unsigned int experimEnabled;
GLOBAL_EXTERN unsigned int lookupCacheEnabled;
GLOBAL_EXTERN unsigned int extended_security;	/* if on, session setup sent 
				with more secure ntlmssp2 challenge/resp */
GLOBAL_EXTERN unsigned int sign_CIFS_PDUs;  /* enable smb packet signing */
GLOBAL_EXTERN unsigned int linuxExtEnabled;/*enable Linux/Unix CIFS extensions*/
GLOBAL_EXTERN unsigned int CIFSMaxBufSize;  /* max size not including hdr */
GLOBAL_EXTERN unsigned int cifs_min_rcv;    /* min size of big ntwrk buf pool */
GLOBAL_EXTERN unsigned int cifs_min_small;  /* min size of small buf pool */
GLOBAL_EXTERN unsigned int cifs_max_pending; /* MAX requests at once to server*/

