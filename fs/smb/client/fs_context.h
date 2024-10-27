/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2020, Microsoft Corporation.
 *
 *   Author(s): Steve French <stfrench@microsoft.com>
 *              David Howells <dhowells@redhat.com>
 */

#ifndef _FS_CONTEXT_H
#define _FS_CONTEXT_H

#include "cifsglob.h"
#include <linux/parser.h>
#include <linux/fs_parser.h>

/* Log errors in fs_context (new mount api) but also in dmesg (old style) */
#define cifs_errorf(fc, fmt, ...)			\
	do {						\
		errorf(fc, fmt, ## __VA_ARGS__);	\
		cifs_dbg(VFS, fmt, ## __VA_ARGS__);	\
	} while (0)

enum smb_version {
	Smb_1 = 1,
	Smb_20,
	Smb_21,
	Smb_30,
	Smb_302,
	Smb_311,
	Smb_3any,
	Smb_default,
	Smb_version_err
};

enum {
	Opt_cache_loose,
	Opt_cache_strict,
	Opt_cache_none,
	Opt_cache_ro,
	Opt_cache_rw,
	Opt_cache_err
};

enum cifs_reparse_parm {
	Opt_reparse_default,
	Opt_reparse_none,
	Opt_reparse_nfs,
	Opt_reparse_wsl,
	Opt_reparse_err
};

enum cifs_symlink_parm {
	Opt_symlink_default,
	Opt_symlink_none,
	Opt_symlink_native,
	Opt_symlink_unix,
	Opt_symlink_mfsymlinks,
	Opt_symlink_sfu,
	Opt_symlink_nfs,
	Opt_symlink_wsl,
	Opt_symlink_err
};

enum cifs_sec_param {
	Opt_sec_krb5,
	Opt_sec_krb5i,
	Opt_sec_krb5p,
	Opt_sec_ntlmsspi,
	Opt_sec_ntlmssp,
	Opt_sec_ntlmv2,
	Opt_sec_ntlmv2i,
	Opt_sec_none,

	Opt_sec_err
};

enum cifs_upcall_target_param {
	Opt_upcall_target_mount,
	Opt_upcall_target_application,
	Opt_upcall_target_err
};

enum cifs_param {
	/* Mount options that take no arguments */
	Opt_user_xattr,
	Opt_forceuid,
	Opt_forcegid,
	Opt_noblocksend,
	Opt_noautotune,
	Opt_nolease,
	Opt_nosparse,
	Opt_hard,
	Opt_soft,
	Opt_perm,
	Opt_nodelete,
	Opt_mapposix,
	Opt_mapchars,
	Opt_nomapchars,
	Opt_sfu,
	Opt_nodfs,
	Opt_posixpaths,
	Opt_unix,
	Opt_nocase,
	Opt_brl,
	Opt_handlecache,
	Opt_forcemandatorylock,
	Opt_setuidfromacl,
	Opt_setuids,
	Opt_dynperm,
	Opt_intr,
	Opt_strictsync,
	Opt_serverino,
	Opt_rwpidforward,
	Opt_cifsacl,
	Opt_acl,
	Opt_locallease,
	Opt_sign,
	Opt_ignore_signature,
	Opt_seal,
	Opt_noac,
	Opt_fsc,
	Opt_mfsymlinks,
	Opt_multiuser,
	Opt_sloppy,
	Opt_nosharesock,
	Opt_persistent,
	Opt_resilient,
	Opt_tcp_nodelay,
	Opt_domainauto,
	Opt_rdma,
	Opt_modesid,
	Opt_rootfs,
	Opt_multichannel,
	Opt_compress,
	Opt_witness,
	Opt_is_upcall_target_mount,
	Opt_is_upcall_target_application,
	Opt_unicode,

	/* Mount options which take numeric value */
	Opt_backupuid,
	Opt_backupgid,
	Opt_uid,
	Opt_cruid,
	Opt_gid,
	Opt_port,
	Opt_file_mode,
	Opt_dirmode,
	Opt_min_enc_offload,
	Opt_retrans,
	Opt_blocksize,
	Opt_rasize,
	Opt_rsize,
	Opt_wsize,
	Opt_actimeo,
	Opt_acdirmax,
	Opt_acregmax,
	Opt_closetimeo,
	Opt_echo_interval,
	Opt_max_credits,
	Opt_max_cached_dirs,
	Opt_snapshot,
	Opt_max_channels,
	Opt_handletimeout,

	/* Mount options which take string value */
	Opt_source,
	Opt_user,
	Opt_pass,
	Opt_pass2,
	Opt_ip,
	Opt_domain,
	Opt_srcaddr,
	Opt_iocharset,
	Opt_netbiosname,
	Opt_servern,
	Opt_nbsessinit,
	Opt_ver,
	Opt_vers,
	Opt_sec,
	Opt_cache,
	Opt_reparse,
	Opt_upcalltarget,
	Opt_nativesocket,
	Opt_symlink,
	Opt_symlinkroot,

	/* Mount options to be ignored */
	Opt_ignore,

	Opt_err
};

struct smb3_fs_context {
	bool forceuid_specified;
	bool forcegid_specified;
	bool uid_specified;
	bool cruid_specified;
	bool gid_specified;
	bool sloppy;
	bool got_ip;
	bool got_version;
	bool got_rsize;
	bool got_wsize;
	bool got_bsize;
	unsigned short port;

	char *username;
	char *password;
	char *password2;
	char *domainname;
	char *source;
	char *server_hostname;
	char *UNC;
	char *nodename;
	char workstation_name[CIFS_MAX_WORKSTATION_LEN];
	char *iocharset;  /* local code page for mapping to and from Unicode */
	char source_rfc1001_name[RFC1001_NAME_LEN_WITH_NULL]; /* clnt nb name */
	char target_rfc1001_name[RFC1001_NAME_LEN_WITH_NULL]; /* srvr nb name */
	int rfc1001_sessinit;
	kuid_t cred_uid;
	kuid_t linux_uid;
	kgid_t linux_gid;
	kuid_t backupuid;
	kgid_t backupgid;
	umode_t file_mode;
	umode_t dir_mode;
	enum securityEnum sectype; /* sectype requested via mnt opts */
	enum upcall_target_enum upcall_target; /* where to upcall for mount */
	bool sign; /* was signing requested via mnt opts? */
	bool ignore_signature:1;
	bool retry:1;
	bool intr:1;
	bool setuids:1;
	bool setuidfromacl:1;
	bool override_uid:1;
	bool override_gid:1;
	bool dynperm:1;
	bool noperm:1;
	bool nodelete:1;
	bool mode_ace:1;
	bool no_psx_acl:1; /* set if posix acl support should be disabled */
	bool cifs_acl:1;
	bool backupuid_specified; /* mount option  backupuid  is specified */
	bool backupgid_specified; /* mount option  backupgid  is specified */
	bool no_xattr:1;   /* set if xattr (EA) support should be disabled*/
	bool server_ino:1; /* use inode numbers from server ie UniqueId */
	bool direct_io:1;
	bool strict_io:1; /* strict cache behavior */
	bool cache_ro:1;
	bool cache_rw:1;
	bool remap:1;      /* set to remap seven reserved chars in filenames */
	bool sfu_remap:1;  /* remap seven reserved chars ala SFU */
	bool posix_paths:1; /* unset to not ask for posix pathnames. */
	bool no_linux_ext:1;
	bool linux_ext:1;
	bool sfu_emul:1;
	bool nullauth:1;   /* attempt to authenticate with null user */
	bool nocase:1;     /* request case insensitive filenames */
	bool nobrl:1;      /* disable sending byte range locks to srv */
	bool nohandlecache:1; /* disable caching dir handles if srvr probs */
	bool mand_lock:1;  /* send mandatory not posix byte range lock reqs */
	bool seal:1;       /* request transport encryption on share */
	bool nodfs:1;      /* Do not request DFS, even if available */
	bool local_lease:1; /* check leases only on local system, not remote */
	bool noblocksnd:1;
	bool noautotune:1;
	bool nostrictsync:1; /* do not force expensive SMBflush on every sync */
	bool no_lease:1;     /* disable requesting leases */
	bool no_sparse:1;    /* do not attempt to set files sparse */
	bool fsc:1;	/* enable fscache */
	bool mfsymlinks:1; /* use Minshall+French Symlinks */
	bool multiuser:1;
	bool rwpidforward:1; /* pid forward for read/write operations */
	bool nosharesock:1;
	bool persistent:1;
	bool nopersistent:1;
	bool resilient:1; /* noresilient not required since not fored for CA */
	bool domainauto:1;
	bool rdma:1;
	bool multichannel:1;
	bool use_client_guid:1;
	/* reuse existing guid for multichannel */
	u8 client_guid[SMB2_CLIENT_GUID_SIZE];
	/* User-specified original r/wsize value */
	unsigned int vol_rsize;
	unsigned int vol_wsize;
	unsigned int bsize;
	unsigned int rasize;
	unsigned int rsize;
	unsigned int wsize;
	unsigned int min_offload;
	unsigned int retrans;
	bool sockopt_tcp_nodelay:1;
	/* attribute cache timeout for files and directories in jiffies */
	unsigned long acregmax;
	unsigned long acdirmax;
	/* timeout for deferred close of files in jiffies */
	unsigned long closetimeo;
	struct smb_version_operations *ops;
	struct smb_version_values *vals;
	char *prepath;
	struct sockaddr_storage dstaddr; /* destination address */
	struct sockaddr_storage srcaddr; /* allow binding to a local IP */
	struct nls_table *local_nls; /* This is a copy of the pointer in cifs_sb */
	unsigned int echo_interval; /* echo interval in secs */
	__u64 snapshot_time; /* needed for timewarp tokens */
	__u32 handle_timeout; /* persistent and durable handle timeout in ms */
	unsigned int max_credits; /* smb3 max_credits 10 < credits < 60000 */
	unsigned int max_channels;
	unsigned int max_cached_dirs;
	bool compress; /* enable SMB2 messages (READ/WRITE) de/compression */
	bool rootfs:1; /* if it's a SMB root file system */
	bool witness:1; /* use witness protocol */
	int unicode;
	char *leaf_fullpath;
	struct cifs_ses *dfs_root_ses;
	bool dfs_automount:1; /* set for dfs automount only */
	enum cifs_reparse_type reparse_type;
	enum cifs_symlink_type symlink_type;
	bool nonativesocket:1;
	bool dfs_conn:1; /* set for dfs mounts */
	char *dns_dom;
	char *symlinkroot; /* top level directory for native SMB symlinks in absolute format */
};

extern const struct fs_parameter_spec smb3_fs_parameters[];

extern enum cifs_symlink_type get_cifs_symlink_type(struct cifs_sb_info *cifs_sb);

extern int smb3_init_fs_context(struct fs_context *fc);
extern void smb3_cleanup_fs_context_contents(struct smb3_fs_context *ctx);
extern void smb3_cleanup_fs_context(struct smb3_fs_context *ctx);

static inline struct smb3_fs_context *smb3_fc2context(const struct fs_context *fc)
{
	return fc->fs_private;
}

extern int smb3_fs_context_dup(struct smb3_fs_context *new_ctx, struct smb3_fs_context *ctx);
extern int smb3_sync_session_ctx_passwords(struct cifs_sb_info *cifs_sb, struct cifs_ses *ses);
extern void smb3_update_mnt_flags(struct cifs_sb_info *cifs_sb);

/*
 * max deferred close timeout (jiffies) - 2^30
 */
#define SMB3_MAX_DCLOSETIMEO (1 << 30)
#define SMB3_DEF_DCLOSETIMEO (1 * HZ) /* even 1 sec enough to help eg open/write/close/open/read */
#define MAX_CACHED_FIDS 16
extern char *cifs_sanitize_prepath(char *prepath, gfp_t gfp);

extern struct mutex cifs_mount_mutex;

static inline void cifs_mount_lock(void)
{
	mutex_lock(&cifs_mount_mutex);
}

static inline void cifs_mount_unlock(void)
{
	mutex_unlock(&cifs_mount_mutex);
}

#endif
