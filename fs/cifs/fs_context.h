/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2020, Microsoft Corporation.
 *
 *   Author(s): Steve French <stfrench@microsoft.com>
 *              David Howells <dhowells@redhat.com>
 */

#ifndef _FS_CONTEXT_H
#define _FS_CONTEXT_H

#include <linux/parser.h>
#include "cifsglob.h"

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

int cifs_parse_smb_version(char *value, struct smb3_fs_context *ctx, bool is_smb3);

enum {
	Opt_cache_loose,
	Opt_cache_strict,
	Opt_cache_none,
	Opt_cache_ro,
	Opt_cache_rw,
	Opt_cache_err
};

int cifs_parse_cache_flavor(char *value, struct smb3_fs_context *ctx);

enum cifs_sec_param {
	Opt_sec_krb5,
	Opt_sec_krb5i,
	Opt_sec_krb5p,
	Opt_sec_ntlmsspi,
	Opt_sec_ntlmssp,
	Opt_ntlm,
	Opt_sec_ntlmi,
	Opt_sec_ntlmv2,
	Opt_sec_ntlmv2i,
	Opt_sec_lanman,
	Opt_sec_none,

	Opt_sec_err
};

struct smb3_fs_context {
	bool uid_specified;
	bool gid_specified;
	bool sloppy;
	char *nodename;
	bool got_ip;
	bool got_version;
	unsigned short port;

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
	unsigned int bsize;
	unsigned int rsize;
	unsigned int wsize;
	unsigned int min_offload;
	bool sockopt_tcp_nodelay:1;
	unsigned long actimeo; /* attribute cache timeout (jiffies) */
	struct smb_version_operations *ops;
	struct smb_version_values *vals;
	char *prepath;
	struct sockaddr_storage dstaddr; /* destination address */
	struct sockaddr_storage srcaddr; /* allow binding to a local IP */
	struct nls_table *local_nls;
	unsigned int echo_interval; /* echo interval in secs */
	__u64 snapshot_time; /* needed for timewarp tokens */
	__u32 handle_timeout; /* persistent and durable handle timeout in ms */
	unsigned int max_credits; /* smb3 max_credits 10 < credits < 60000 */
	unsigned int max_channels;
	__u16 compression; /* compression algorithm 0xFFFF default 0=disabled */
	bool rootfs:1; /* if it's a SMB root file system */
};

int cifs_parse_security_flavors(char *value, struct smb3_fs_context *ctx);

#endif
