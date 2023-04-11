// SPDX-License-Identifier: LGPL-2.1
/*
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   Common Internet FileSystem (CIFS) client
 *
 */

/* Note that BB means BUGBUG (ie something to fix eventually) */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/filelock.h>
#include <linux/mount.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/seq_file.h>
#include <linux/vfs.h>
#include <linux/mempool.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/namei.h>
#include <linux/random.h>
#include <linux/uuid.h>
#include <linux/xattr.h>
#include <uapi/linux/magic.h>
#include <net/ipv6.h>
#include "cifsfs.h"
#include "cifspdu.h"
#define DECLARE_GLOBALS_HERE
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include <linux/mm.h>
#include <linux/key-type.h>
#include "cifs_spnego.h"
#include "fscache.h"
#ifdef CONFIG_CIFS_DFS_UPCALL
#include "dfs_cache.h"
#endif
#ifdef CONFIG_CIFS_SWN_UPCALL
#include "netlink.h"
#endif
#include "fs_context.h"
#include "cached_dir.h"

/*
 * DOS dates from 1980/1/1 through 2107/12/31
 * Protocol specifications indicate the range should be to 119, which
 * limits maximum year to 2099. But this range has not been checked.
 */
#define SMB_DATE_MAX (127<<9 | 12<<5 | 31)
#define SMB_DATE_MIN (0<<9 | 1<<5 | 1)
#define SMB_TIME_MAX (23<<11 | 59<<5 | 29)

int cifsFYI = 0;
bool traceSMB;
bool enable_oplocks = true;
bool linuxExtEnabled = true;
bool lookupCacheEnabled = true;
bool disable_legacy_dialects; /* false by default */
bool enable_gcm_256 = true;
bool require_gcm_256; /* false by default */
bool enable_negotiate_signing; /* false by default */
unsigned int global_secflags = CIFSSEC_DEF;
/* unsigned int ntlmv2_support = 0; */
unsigned int sign_CIFS_PDUs = 1;

/*
 * Global transaction id (XID) information
 */
unsigned int GlobalCurrentXid;	/* protected by GlobalMid_Sem */
unsigned int GlobalTotalActiveXid; /* prot by GlobalMid_Sem */
unsigned int GlobalMaxActiveXid;	/* prot by GlobalMid_Sem */
spinlock_t GlobalMid_Lock; /* protects above & list operations on midQ entries */

/*
 *  Global counters, updated atomically
 */
atomic_t sesInfoAllocCount;
atomic_t tconInfoAllocCount;
atomic_t tcpSesNextId;
atomic_t tcpSesAllocCount;
atomic_t tcpSesReconnectCount;
atomic_t tconInfoReconnectCount;

atomic_t mid_count;
atomic_t buf_alloc_count;
atomic_t small_buf_alloc_count;
#ifdef CONFIG_CIFS_STATS2
atomic_t total_buf_alloc_count;
atomic_t total_small_buf_alloc_count;
#endif/* STATS2 */
struct list_head	cifs_tcp_ses_list;
spinlock_t		cifs_tcp_ses_lock;
static const struct super_operations cifs_super_ops;
unsigned int CIFSMaxBufSize = CIFS_MAX_MSGSIZE;
module_param(CIFSMaxBufSize, uint, 0444);
MODULE_PARM_DESC(CIFSMaxBufSize, "Network buffer size (not including header) "
				 "for CIFS requests. "
				 "Default: 16384 Range: 8192 to 130048");
unsigned int cifs_min_rcv = CIFS_MIN_RCV_POOL;
module_param(cifs_min_rcv, uint, 0444);
MODULE_PARM_DESC(cifs_min_rcv, "Network buffers in pool. Default: 4 Range: "
				"1 to 64");
unsigned int cifs_min_small = 30;
module_param(cifs_min_small, uint, 0444);
MODULE_PARM_DESC(cifs_min_small, "Small network buffers in pool. Default: 30 "
				 "Range: 2 to 256");
unsigned int cifs_max_pending = CIFS_MAX_REQ;
module_param(cifs_max_pending, uint, 0444);
MODULE_PARM_DESC(cifs_max_pending, "Simultaneous requests to server for "
				   "CIFS/SMB1 dialect (N/A for SMB3) "
				   "Default: 32767 Range: 2 to 32767.");
#ifdef CONFIG_CIFS_STATS2
unsigned int slow_rsp_threshold = 1;
module_param(slow_rsp_threshold, uint, 0644);
MODULE_PARM_DESC(slow_rsp_threshold, "Amount of time (in seconds) to wait "
				   "before logging that a response is delayed. "
				   "Default: 1 (if set to 0 disables msg).");
#endif /* STATS2 */

module_param(enable_oplocks, bool, 0644);
MODULE_PARM_DESC(enable_oplocks, "Enable or disable oplocks. Default: y/Y/1");

module_param(enable_gcm_256, bool, 0644);
MODULE_PARM_DESC(enable_gcm_256, "Enable requesting strongest (256 bit) GCM encryption. Default: n/N/0");

module_param(require_gcm_256, bool, 0644);
MODULE_PARM_DESC(require_gcm_256, "Require strongest (256 bit) GCM encryption. Default: n/N/0");

module_param(enable_negotiate_signing, bool, 0644);
MODULE_PARM_DESC(enable_negotiate_signing, "Enable negotiating packet signing algorithm with server. Default: n/N/0");

module_param(disable_legacy_dialects, bool, 0644);
MODULE_PARM_DESC(disable_legacy_dialects, "To improve security it may be "
				  "helpful to restrict the ability to "
				  "override the default dialects (SMB2.1, "
				  "SMB3 and SMB3.02) on mount with old "
				  "dialects (CIFS/SMB1 and SMB2) since "
				  "vers=1.0 (CIFS/SMB1) and vers=2.0 are weaker"
				  " and less secure. Default: n/N/0");

extern mempool_t *cifs_sm_req_poolp;
extern mempool_t *cifs_req_poolp;
extern mempool_t *cifs_mid_poolp;

struct workqueue_struct	*cifsiod_wq;
struct workqueue_struct	*decrypt_wq;
struct workqueue_struct	*fileinfo_put_wq;
struct workqueue_struct	*cifsoplockd_wq;
struct workqueue_struct	*deferredclose_wq;
__u32 cifs_lock_secret;

/*
 * Bumps refcount for cifs super block.
 * Note that it should be only called if a referece to VFS super block is
 * already held, e.g. in open-type syscalls context. Otherwise it can race with
 * atomic_dec_and_test in deactivate_locked_super.
 */
void
cifs_sb_active(struct super_block *sb)
{
	struct cifs_sb_info *server = CIFS_SB(sb);

	if (atomic_inc_return(&server->active) == 1)
		atomic_inc(&sb->s_active);
}

void
cifs_sb_deactive(struct super_block *sb)
{
	struct cifs_sb_info *server = CIFS_SB(sb);

	if (atomic_dec_and_test(&server->active))
		deactivate_super(sb);
}

static int
cifs_read_super(struct super_block *sb)
{
	struct inode *inode;
	struct cifs_sb_info *cifs_sb;
	struct cifs_tcon *tcon;
	struct timespec64 ts;
	int rc = 0;

	cifs_sb = CIFS_SB(sb);
	tcon = cifs_sb_master_tcon(cifs_sb);

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_POSIXACL)
		sb->s_flags |= SB_POSIXACL;

	if (tcon->snapshot_time)
		sb->s_flags |= SB_RDONLY;

	if (tcon->ses->capabilities & tcon->ses->server->vals->cap_large_files)
		sb->s_maxbytes = MAX_LFS_FILESIZE;
	else
		sb->s_maxbytes = MAX_NON_LFS;

	/*
	 * Some very old servers like DOS and OS/2 used 2 second granularity
	 * (while all current servers use 100ns granularity - see MS-DTYP)
	 * but 1 second is the maximum allowed granularity for the VFS
	 * so for old servers set time granularity to 1 second while for
	 * everything else (current servers) set it to 100ns.
	 */
	if ((tcon->ses->server->vals->protocol_id == SMB10_PROT_ID) &&
	    ((tcon->ses->capabilities &
	      tcon->ses->server->vals->cap_nt_find) == 0) &&
	    !tcon->unix_ext) {
		sb->s_time_gran = 1000000000; /* 1 second is max allowed gran */
		ts = cnvrtDosUnixTm(cpu_to_le16(SMB_DATE_MIN), 0, 0);
		sb->s_time_min = ts.tv_sec;
		ts = cnvrtDosUnixTm(cpu_to_le16(SMB_DATE_MAX),
				    cpu_to_le16(SMB_TIME_MAX), 0);
		sb->s_time_max = ts.tv_sec;
	} else {
		/*
		 * Almost every server, including all SMB2+, uses DCE TIME
		 * ie 100 nanosecond units, since 1601.  See MS-DTYP and MS-FSCC
		 */
		sb->s_time_gran = 100;
		ts = cifs_NTtimeToUnix(0);
		sb->s_time_min = ts.tv_sec;
		ts = cifs_NTtimeToUnix(cpu_to_le64(S64_MAX));
		sb->s_time_max = ts.tv_sec;
	}

	sb->s_magic = CIFS_SUPER_MAGIC;
	sb->s_op = &cifs_super_ops;
	sb->s_xattr = cifs_xattr_handlers;
	rc = super_setup_bdi(sb);
	if (rc)
		goto out_no_root;
	/* tune readahead according to rsize if readahead size not set on mount */
	if (cifs_sb->ctx->rsize == 0)
		cifs_sb->ctx->rsize =
			tcon->ses->server->ops->negotiate_rsize(tcon, cifs_sb->ctx);
	if (cifs_sb->ctx->rasize)
		sb->s_bdi->ra_pages = cifs_sb->ctx->rasize / PAGE_SIZE;
	else
		sb->s_bdi->ra_pages = cifs_sb->ctx->rsize / PAGE_SIZE;

	sb->s_blocksize = CIFS_MAX_MSGSIZE;
	sb->s_blocksize_bits = 14;	/* default 2**14 = CIFS_MAX_MSGSIZE */
	inode = cifs_root_iget(sb);

	if (IS_ERR(inode)) {
		rc = PTR_ERR(inode);
		goto out_no_root;
	}

	if (tcon->nocase)
		sb->s_d_op = &cifs_ci_dentry_ops;
	else
		sb->s_d_op = &cifs_dentry_ops;

	sb->s_root = d_make_root(inode);
	if (!sb->s_root) {
		rc = -ENOMEM;
		goto out_no_root;
	}

#ifdef CONFIG_CIFS_NFSD_EXPORT
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM) {
		cifs_dbg(FYI, "export ops supported\n");
		sb->s_export_op = &cifs_export_ops;
	}
#endif /* CONFIG_CIFS_NFSD_EXPORT */

	return 0;

out_no_root:
	cifs_dbg(VFS, "%s: get root inode failed\n", __func__);
	return rc;
}

static void cifs_kill_sb(struct super_block *sb)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);

	/*
	 * We ned to release all dentries for the cached directories
	 * before we kill the sb.
	 */
	if (cifs_sb->root) {
		close_all_cached_dirs(cifs_sb);

		/* finally release root dentry */
		dput(cifs_sb->root);
		cifs_sb->root = NULL;
	}

	kill_anon_super(sb);
	cifs_umount(cifs_sb);
}

static int
cifs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifs_tcon *tcon = cifs_sb_master_tcon(cifs_sb);
	struct TCP_Server_Info *server = tcon->ses->server;
	unsigned int xid;
	int rc = 0;

	xid = get_xid();

	if (le32_to_cpu(tcon->fsAttrInfo.MaxPathNameComponentLength) > 0)
		buf->f_namelen =
		       le32_to_cpu(tcon->fsAttrInfo.MaxPathNameComponentLength);
	else
		buf->f_namelen = PATH_MAX;

	buf->f_fsid.val[0] = tcon->vol_serial_number;
	/* are using part of create time for more randomness, see man statfs */
	buf->f_fsid.val[1] =  (int)le64_to_cpu(tcon->vol_create_time);

	buf->f_files = 0;	/* undefined */
	buf->f_ffree = 0;	/* unlimited */

	if (server->ops->queryfs)
		rc = server->ops->queryfs(xid, tcon, cifs_sb, buf);

	free_xid(xid);
	return rc;
}

static long cifs_fallocate(struct file *file, int mode, loff_t off, loff_t len)
{
	struct cifs_sb_info *cifs_sb = CIFS_FILE_SB(file);
	struct cifs_tcon *tcon = cifs_sb_master_tcon(cifs_sb);
	struct TCP_Server_Info *server = tcon->ses->server;

	if (server->ops->fallocate)
		return server->ops->fallocate(file, tcon, mode, off, len);

	return -EOPNOTSUPP;
}

static int cifs_permission(struct mnt_idmap *idmap,
			   struct inode *inode, int mask)
{
	struct cifs_sb_info *cifs_sb;

	cifs_sb = CIFS_SB(inode->i_sb);

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_PERM) {
		if ((mask & MAY_EXEC) && !execute_ok(inode))
			return -EACCES;
		else
			return 0;
	} else /* file mode might have been restricted at mount time
		on the client (above and beyond ACL on servers) for
		servers which do not support setting and viewing mode bits,
		so allowing client to check permissions is useful */
		return generic_permission(&nop_mnt_idmap, inode, mask);
}

static struct kmem_cache *cifs_inode_cachep;
static struct kmem_cache *cifs_req_cachep;
static struct kmem_cache *cifs_mid_cachep;
static struct kmem_cache *cifs_sm_req_cachep;
mempool_t *cifs_sm_req_poolp;
mempool_t *cifs_req_poolp;
mempool_t *cifs_mid_poolp;

static struct inode *
cifs_alloc_inode(struct super_block *sb)
{
	struct cifsInodeInfo *cifs_inode;
	cifs_inode = alloc_inode_sb(sb, cifs_inode_cachep, GFP_KERNEL);
	if (!cifs_inode)
		return NULL;
	cifs_inode->cifsAttrs = 0x20;	/* default */
	cifs_inode->time = 0;
	/*
	 * Until the file is open and we have gotten oplock info back from the
	 * server, can not assume caching of file data or metadata.
	 */
	cifs_set_oplock_level(cifs_inode, 0);
	cifs_inode->flags = 0;
	spin_lock_init(&cifs_inode->writers_lock);
	cifs_inode->writers = 0;
	cifs_inode->netfs.inode.i_blkbits = 14;  /* 2**14 = CIFS_MAX_MSGSIZE */
	cifs_inode->server_eof = 0;
	cifs_inode->uniqueid = 0;
	cifs_inode->createtime = 0;
	cifs_inode->epoch = 0;
	spin_lock_init(&cifs_inode->open_file_lock);
	generate_random_uuid(cifs_inode->lease_key);
	cifs_inode->symlink_target = NULL;

	/*
	 * Can not set i_flags here - they get immediately overwritten to zero
	 * by the VFS.
	 */
	/* cifs_inode->netfs.inode.i_flags = S_NOATIME | S_NOCMTIME; */
	INIT_LIST_HEAD(&cifs_inode->openFileList);
	INIT_LIST_HEAD(&cifs_inode->llist);
	INIT_LIST_HEAD(&cifs_inode->deferred_closes);
	spin_lock_init(&cifs_inode->deferred_lock);
	return &cifs_inode->netfs.inode;
}

static void
cifs_free_inode(struct inode *inode)
{
	struct cifsInodeInfo *cinode = CIFS_I(inode);

	if (S_ISLNK(inode->i_mode))
		kfree(cinode->symlink_target);
	kmem_cache_free(cifs_inode_cachep, cinode);
}

static void
cifs_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	if (inode->i_state & I_PINNING_FSCACHE_WB)
		cifs_fscache_unuse_inode_cookie(inode, true);
	cifs_fscache_release_inode_cookie(inode);
	clear_inode(inode);
}

static void
cifs_show_address(struct seq_file *s, struct TCP_Server_Info *server)
{
	struct sockaddr_in *sa = (struct sockaddr_in *) &server->dstaddr;
	struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *) &server->dstaddr;

	seq_puts(s, ",addr=");

	switch (server->dstaddr.ss_family) {
	case AF_INET:
		seq_printf(s, "%pI4", &sa->sin_addr.s_addr);
		break;
	case AF_INET6:
		seq_printf(s, "%pI6", &sa6->sin6_addr.s6_addr);
		if (sa6->sin6_scope_id)
			seq_printf(s, "%%%u", sa6->sin6_scope_id);
		break;
	default:
		seq_puts(s, "(unknown)");
	}
	if (server->rdma)
		seq_puts(s, ",rdma");
}

static void
cifs_show_security(struct seq_file *s, struct cifs_ses *ses)
{
	if (ses->sectype == Unspecified) {
		if (ses->user_name == NULL)
			seq_puts(s, ",sec=none");
		return;
	}

	seq_puts(s, ",sec=");

	switch (ses->sectype) {
	case NTLMv2:
		seq_puts(s, "ntlmv2");
		break;
	case Kerberos:
		seq_puts(s, "krb5");
		break;
	case RawNTLMSSP:
		seq_puts(s, "ntlmssp");
		break;
	default:
		/* shouldn't ever happen */
		seq_puts(s, "unknown");
		break;
	}

	if (ses->sign)
		seq_puts(s, "i");

	if (ses->sectype == Kerberos)
		seq_printf(s, ",cruid=%u",
			   from_kuid_munged(&init_user_ns, ses->cred_uid));
}

static void
cifs_show_cache_flavor(struct seq_file *s, struct cifs_sb_info *cifs_sb)
{
	seq_puts(s, ",cache=");

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_STRICT_IO)
		seq_puts(s, "strict");
	else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DIRECT_IO)
		seq_puts(s, "none");
	else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_RW_CACHE)
		seq_puts(s, "singleclient"); /* assume only one client access */
	else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_RO_CACHE)
		seq_puts(s, "ro"); /* read only caching assumed */
	else
		seq_puts(s, "loose");
}

/*
 * cifs_show_devname() is used so we show the mount device name with correct
 * format (e.g. forward slashes vs. back slashes) in /proc/mounts
 */
static int cifs_show_devname(struct seq_file *m, struct dentry *root)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(root->d_sb);
	char *devname = kstrdup(cifs_sb->ctx->source, GFP_KERNEL);

	if (devname == NULL)
		seq_puts(m, "none");
	else {
		convert_delimiter(devname, '/');
		/* escape all spaces in share names */
		seq_escape(m, devname, " \t");
		kfree(devname);
	}
	return 0;
}

/*
 * cifs_show_options() is for displaying mount options in /proc/mounts.
 * Not all settable options are displayed but most of the important
 * ones are.
 */
static int
cifs_show_options(struct seq_file *s, struct dentry *root)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(root->d_sb);
	struct cifs_tcon *tcon = cifs_sb_master_tcon(cifs_sb);
	struct sockaddr *srcaddr;
	srcaddr = (struct sockaddr *)&tcon->ses->server->srcaddr;

	seq_show_option(s, "vers", tcon->ses->server->vals->version_string);
	cifs_show_security(s, tcon->ses);
	cifs_show_cache_flavor(s, cifs_sb);

	if (tcon->no_lease)
		seq_puts(s, ",nolease");
	if (cifs_sb->ctx->multiuser)
		seq_puts(s, ",multiuser");
	else if (tcon->ses->user_name)
		seq_show_option(s, "username", tcon->ses->user_name);

	if (tcon->ses->domainName && tcon->ses->domainName[0] != 0)
		seq_show_option(s, "domain", tcon->ses->domainName);

	if (srcaddr->sa_family != AF_UNSPEC) {
		struct sockaddr_in *saddr4;
		struct sockaddr_in6 *saddr6;
		saddr4 = (struct sockaddr_in *)srcaddr;
		saddr6 = (struct sockaddr_in6 *)srcaddr;
		if (srcaddr->sa_family == AF_INET6)
			seq_printf(s, ",srcaddr=%pI6c",
				   &saddr6->sin6_addr);
		else if (srcaddr->sa_family == AF_INET)
			seq_printf(s, ",srcaddr=%pI4",
				   &saddr4->sin_addr.s_addr);
		else
			seq_printf(s, ",srcaddr=BAD-AF:%i",
				   (int)(srcaddr->sa_family));
	}

	seq_printf(s, ",uid=%u",
		   from_kuid_munged(&init_user_ns, cifs_sb->ctx->linux_uid));
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_OVERR_UID)
		seq_puts(s, ",forceuid");
	else
		seq_puts(s, ",noforceuid");

	seq_printf(s, ",gid=%u",
		   from_kgid_munged(&init_user_ns, cifs_sb->ctx->linux_gid));
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_OVERR_GID)
		seq_puts(s, ",forcegid");
	else
		seq_puts(s, ",noforcegid");

	cifs_show_address(s, tcon->ses->server);

	if (!tcon->unix_ext)
		seq_printf(s, ",file_mode=0%ho,dir_mode=0%ho",
					   cifs_sb->ctx->file_mode,
					   cifs_sb->ctx->dir_mode);
	if (cifs_sb->ctx->iocharset)
		seq_printf(s, ",iocharset=%s", cifs_sb->ctx->iocharset);
	if (tcon->seal)
		seq_puts(s, ",seal");
	else if (tcon->ses->server->ignore_signature)
		seq_puts(s, ",signloosely");
	if (tcon->nocase)
		seq_puts(s, ",nocase");
	if (tcon->nodelete)
		seq_puts(s, ",nodelete");
	if (cifs_sb->ctx->no_sparse)
		seq_puts(s, ",nosparse");
	if (tcon->local_lease)
		seq_puts(s, ",locallease");
	if (tcon->retry)
		seq_puts(s, ",hard");
	else
		seq_puts(s, ",soft");
	if (tcon->use_persistent)
		seq_puts(s, ",persistenthandles");
	else if (tcon->use_resilient)
		seq_puts(s, ",resilienthandles");
	if (tcon->posix_extensions)
		seq_puts(s, ",posix");
	else if (tcon->unix_ext)
		seq_puts(s, ",unix");
	else
		seq_puts(s, ",nounix");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_DFS)
		seq_puts(s, ",nodfs");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_POSIX_PATHS)
		seq_puts(s, ",posixpaths");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID)
		seq_puts(s, ",setuids");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UID_FROM_ACL)
		seq_puts(s, ",idsfromsid");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM)
		seq_puts(s, ",serverino");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_RWPIDFORWARD)
		seq_puts(s, ",rwpidforward");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOPOSIXBRL)
		seq_puts(s, ",forcemand");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_XATTR)
		seq_puts(s, ",nouser_xattr");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR)
		seq_puts(s, ",mapchars");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SFM_CHR)
		seq_puts(s, ",mapposix");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL)
		seq_puts(s, ",sfu");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
		seq_puts(s, ",nobrl");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_HANDLE_CACHE)
		seq_puts(s, ",nohandlecache");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MODE_FROM_SID)
		seq_puts(s, ",modefromsid");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_ACL)
		seq_puts(s, ",cifsacl");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DYNPERM)
		seq_puts(s, ",dynperm");
	if (root->d_sb->s_flags & SB_POSIXACL)
		seq_puts(s, ",acl");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MF_SYMLINKS)
		seq_puts(s, ",mfsymlinks");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_FSCACHE)
		seq_puts(s, ",fsc");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOSSYNC)
		seq_puts(s, ",nostrictsync");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_PERM)
		seq_puts(s, ",noperm");
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_BACKUPUID)
		seq_printf(s, ",backupuid=%u",
			   from_kuid_munged(&init_user_ns,
					    cifs_sb->ctx->backupuid));
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_BACKUPGID)
		seq_printf(s, ",backupgid=%u",
			   from_kgid_munged(&init_user_ns,
					    cifs_sb->ctx->backupgid));

	seq_printf(s, ",rsize=%u", cifs_sb->ctx->rsize);
	seq_printf(s, ",wsize=%u", cifs_sb->ctx->wsize);
	seq_printf(s, ",bsize=%u", cifs_sb->ctx->bsize);
	if (cifs_sb->ctx->rasize)
		seq_printf(s, ",rasize=%u", cifs_sb->ctx->rasize);
	if (tcon->ses->server->min_offload)
		seq_printf(s, ",esize=%u", tcon->ses->server->min_offload);
	seq_printf(s, ",echo_interval=%lu",
			tcon->ses->server->echo_interval / HZ);

	/* Only display the following if overridden on mount */
	if (tcon->ses->server->max_credits != SMB2_MAX_CREDITS_AVAILABLE)
		seq_printf(s, ",max_credits=%u", tcon->ses->server->max_credits);
	if (tcon->ses->server->tcp_nodelay)
		seq_puts(s, ",tcpnodelay");
	if (tcon->ses->server->noautotune)
		seq_puts(s, ",noautotune");
	if (tcon->ses->server->noblocksnd)
		seq_puts(s, ",noblocksend");

	if (tcon->snapshot_time)
		seq_printf(s, ",snapshot=%llu", tcon->snapshot_time);
	if (tcon->handle_timeout)
		seq_printf(s, ",handletimeout=%u", tcon->handle_timeout);

	/*
	 * Display file and directory attribute timeout in seconds.
	 * If file and directory attribute timeout the same then actimeo
	 * was likely specified on mount
	 */
	if (cifs_sb->ctx->acdirmax == cifs_sb->ctx->acregmax)
		seq_printf(s, ",actimeo=%lu", cifs_sb->ctx->acregmax / HZ);
	else {
		seq_printf(s, ",acdirmax=%lu", cifs_sb->ctx->acdirmax / HZ);
		seq_printf(s, ",acregmax=%lu", cifs_sb->ctx->acregmax / HZ);
	}
	seq_printf(s, ",closetimeo=%lu", cifs_sb->ctx->closetimeo / HZ);

	if (tcon->ses->chan_max > 1)
		seq_printf(s, ",multichannel,max_channels=%zu",
			   tcon->ses->chan_max);

	if (tcon->use_witness)
		seq_puts(s, ",witness");

	return 0;
}

static void cifs_umount_begin(struct super_block *sb)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifs_tcon *tcon;

	if (cifs_sb == NULL)
		return;

	tcon = cifs_sb_master_tcon(cifs_sb);

	spin_lock(&cifs_tcp_ses_lock);
	spin_lock(&tcon->tc_lock);
	if ((tcon->tc_count > 1) || (tcon->status == TID_EXITING)) {
		/* we have other mounts to same share or we have
		   already tried to umount this and woken up
		   all waiting network requests, nothing to do */
		spin_unlock(&tcon->tc_lock);
		spin_unlock(&cifs_tcp_ses_lock);
		return;
	}
	/*
	 * can not set tcon->status to TID_EXITING yet since we don't know if umount -f will
	 * fail later (e.g. due to open files).  TID_EXITING will be set just before tdis req sent
	 */
	spin_unlock(&tcon->tc_lock);
	spin_unlock(&cifs_tcp_ses_lock);

	/* cancel_brl_requests(tcon); */ /* BB mark all brl mids as exiting */
	/* cancel_notify_requests(tcon); */
	if (tcon->ses && tcon->ses->server) {
		cifs_dbg(FYI, "wake up tasks now - umount begin not complete\n");
		wake_up_all(&tcon->ses->server->request_q);
		wake_up_all(&tcon->ses->server->response_q);
		msleep(1); /* yield */
		/* we have to kick the requests once more */
		wake_up_all(&tcon->ses->server->response_q);
		msleep(1);
	}

	return;
}

#ifdef CONFIG_CIFS_STATS2
static int cifs_show_stats(struct seq_file *s, struct dentry *root)
{
	/* BB FIXME */
	return 0;
}
#endif

static int cifs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	fscache_unpin_writeback(wbc, cifs_inode_cookie(inode));
	return 0;
}

static int cifs_drop_inode(struct inode *inode)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);

	/* no serverino => unconditional eviction */
	return !(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM) ||
		generic_drop_inode(inode);
}

static const struct super_operations cifs_super_ops = {
	.statfs = cifs_statfs,
	.alloc_inode = cifs_alloc_inode,
	.write_inode	= cifs_write_inode,
	.free_inode = cifs_free_inode,
	.drop_inode	= cifs_drop_inode,
	.evict_inode	= cifs_evict_inode,
/*	.show_path	= cifs_show_path, */ /* Would we ever need show path? */
	.show_devname   = cifs_show_devname,
/*	.delete_inode	= cifs_delete_inode,  */  /* Do not need above
	function unless later we add lazy close of inodes or unless the
	kernel forgets to call us with the same number of releases (closes)
	as opens */
	.show_options = cifs_show_options,
	.umount_begin   = cifs_umount_begin,
#ifdef CONFIG_CIFS_STATS2
	.show_stats = cifs_show_stats,
#endif
};

/*
 * Get root dentry from superblock according to prefix path mount option.
 * Return dentry with refcount + 1 on success and NULL otherwise.
 */
static struct dentry *
cifs_get_root(struct smb3_fs_context *ctx, struct super_block *sb)
{
	struct dentry *dentry;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	char *full_path = NULL;
	char *s, *p;
	char sep;

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_USE_PREFIX_PATH)
		return dget(sb->s_root);

	full_path = cifs_build_path_to_root(ctx, cifs_sb,
				cifs_sb_master_tcon(cifs_sb), 0);
	if (full_path == NULL)
		return ERR_PTR(-ENOMEM);

	cifs_dbg(FYI, "Get root dentry for %s\n", full_path);

	sep = CIFS_DIR_SEP(cifs_sb);
	dentry = dget(sb->s_root);
	s = full_path;

	do {
		struct inode *dir = d_inode(dentry);
		struct dentry *child;

		if (!S_ISDIR(dir->i_mode)) {
			dput(dentry);
			dentry = ERR_PTR(-ENOTDIR);
			break;
		}

		/* skip separators */
		while (*s == sep)
			s++;
		if (!*s)
			break;
		p = s++;
		/* next separator */
		while (*s && *s != sep)
			s++;

		child = lookup_positive_unlocked(p, dentry, s - p);
		dput(dentry);
		dentry = child;
	} while (!IS_ERR(dentry));
	kfree(full_path);
	return dentry;
}

static int cifs_set_super(struct super_block *sb, void *data)
{
	struct cifs_mnt_data *mnt_data = data;
	sb->s_fs_info = mnt_data->cifs_sb;
	return set_anon_super(sb, NULL);
}

struct dentry *
cifs_smb3_do_mount(struct file_system_type *fs_type,
	      int flags, struct smb3_fs_context *old_ctx)
{
	int rc;
	struct super_block *sb = NULL;
	struct cifs_sb_info *cifs_sb = NULL;
	struct cifs_mnt_data mnt_data;
	struct dentry *root;

	/*
	 * Prints in Kernel / CIFS log the attempted mount operation
	 *	If CIFS_DEBUG && cifs_FYI
	 */
	if (cifsFYI)
		cifs_dbg(FYI, "Devname: %s flags: %d\n", old_ctx->UNC, flags);
	else
		cifs_info("Attempting to mount %s\n", old_ctx->UNC);

	cifs_sb = kzalloc(sizeof(struct cifs_sb_info), GFP_KERNEL);
	if (cifs_sb == NULL) {
		root = ERR_PTR(-ENOMEM);
		goto out;
	}

	cifs_sb->ctx = kzalloc(sizeof(struct smb3_fs_context), GFP_KERNEL);
	if (!cifs_sb->ctx) {
		root = ERR_PTR(-ENOMEM);
		goto out;
	}
	rc = smb3_fs_context_dup(cifs_sb->ctx, old_ctx);
	if (rc) {
		root = ERR_PTR(rc);
		goto out;
	}

	rc = cifs_setup_cifs_sb(cifs_sb);
	if (rc) {
		root = ERR_PTR(rc);
		goto out;
	}

	rc = cifs_mount(cifs_sb, cifs_sb->ctx);
	if (rc) {
		if (!(flags & SB_SILENT))
			cifs_dbg(VFS, "cifs_mount failed w/return code = %d\n",
				 rc);
		root = ERR_PTR(rc);
		goto out;
	}

	mnt_data.ctx = cifs_sb->ctx;
	mnt_data.cifs_sb = cifs_sb;
	mnt_data.flags = flags;

	/* BB should we make this contingent on mount parm? */
	flags |= SB_NODIRATIME | SB_NOATIME;

	sb = sget(fs_type, cifs_match_super, cifs_set_super, flags, &mnt_data);
	if (IS_ERR(sb)) {
		root = ERR_CAST(sb);
		cifs_umount(cifs_sb);
		cifs_sb = NULL;
		goto out;
	}

	if (sb->s_root) {
		cifs_dbg(FYI, "Use existing superblock\n");
		cifs_umount(cifs_sb);
		cifs_sb = NULL;
	} else {
		rc = cifs_read_super(sb);
		if (rc) {
			root = ERR_PTR(rc);
			goto out_super;
		}

		sb->s_flags |= SB_ACTIVE;
	}

	root = cifs_get_root(cifs_sb ? cifs_sb->ctx : old_ctx, sb);
	if (IS_ERR(root))
		goto out_super;

	if (cifs_sb)
		cifs_sb->root = dget(root);

	cifs_dbg(FYI, "dentry root is: %p\n", root);
	return root;

out_super:
	deactivate_locked_super(sb);
	return root;
out:
	if (cifs_sb) {
		if (!sb || IS_ERR(sb)) {  /* otherwise kill_sb will handle */
			kfree(cifs_sb->prepath);
			smb3_cleanup_fs_context(cifs_sb->ctx);
			kfree(cifs_sb);
		}
	}
	return root;
}


static ssize_t
cifs_loose_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	ssize_t rc;
	struct inode *inode = file_inode(iocb->ki_filp);

	if (iocb->ki_flags & IOCB_DIRECT)
		return cifs_user_readv(iocb, iter);

	rc = cifs_revalidate_mapping(inode);
	if (rc)
		return rc;

	return generic_file_read_iter(iocb, iter);
}

static ssize_t cifs_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct cifsInodeInfo *cinode = CIFS_I(inode);
	ssize_t written;
	int rc;

	if (iocb->ki_filp->f_flags & O_DIRECT) {
		written = cifs_user_writev(iocb, from);
		if (written > 0 && CIFS_CACHE_READ(cinode)) {
			cifs_zap_mapping(inode);
			cifs_dbg(FYI,
				 "Set no oplock for inode=%p after a write operation\n",
				 inode);
			cinode->oplock = 0;
		}
		return written;
	}

	written = cifs_get_writer(cinode);
	if (written)
		return written;

	written = generic_file_write_iter(iocb, from);

	if (CIFS_CACHE_WRITE(CIFS_I(inode)))
		goto out;

	rc = filemap_fdatawrite(inode->i_mapping);
	if (rc)
		cifs_dbg(FYI, "cifs_file_write_iter: %d rc on %p inode\n",
			 rc, inode);

out:
	cifs_put_writer(cinode);
	return written;
}

static loff_t cifs_llseek(struct file *file, loff_t offset, int whence)
{
	struct cifsFileInfo *cfile = file->private_data;
	struct cifs_tcon *tcon;

	/*
	 * whence == SEEK_END || SEEK_DATA || SEEK_HOLE => we must revalidate
	 * the cached file length
	 */
	if (whence != SEEK_SET && whence != SEEK_CUR) {
		int rc;
		struct inode *inode = file_inode(file);

		/*
		 * We need to be sure that all dirty pages are written and the
		 * server has the newest file length.
		 */
		if (!CIFS_CACHE_READ(CIFS_I(inode)) && inode->i_mapping &&
		    inode->i_mapping->nrpages != 0) {
			rc = filemap_fdatawait(inode->i_mapping);
			if (rc) {
				mapping_set_error(inode->i_mapping, rc);
				return rc;
			}
		}
		/*
		 * Some applications poll for the file length in this strange
		 * way so we must seek to end on non-oplocked files by
		 * setting the revalidate time to zero.
		 */
		CIFS_I(inode)->time = 0;

		rc = cifs_revalidate_file_attr(file);
		if (rc < 0)
			return (loff_t)rc;
	}
	if (cfile && cfile->tlink) {
		tcon = tlink_tcon(cfile->tlink);
		if (tcon->ses->server->ops->llseek)
			return tcon->ses->server->ops->llseek(file, tcon,
							      offset, whence);
	}
	return generic_file_llseek(file, offset, whence);
}

static int
cifs_setlease(struct file *file, long arg, struct file_lock **lease, void **priv)
{
	/*
	 * Note that this is called by vfs setlease with i_lock held to
	 * protect *lease from going away.
	 */
	struct inode *inode = file_inode(file);
	struct cifsFileInfo *cfile = file->private_data;

	if (!(S_ISREG(inode->i_mode)))
		return -EINVAL;

	/* Check if file is oplocked if this is request for new lease */
	if (arg == F_UNLCK ||
	    ((arg == F_RDLCK) && CIFS_CACHE_READ(CIFS_I(inode))) ||
	    ((arg == F_WRLCK) && CIFS_CACHE_WRITE(CIFS_I(inode))))
		return generic_setlease(file, arg, lease, priv);
	else if (tlink_tcon(cfile->tlink)->local_lease &&
		 !CIFS_CACHE_READ(CIFS_I(inode)))
		/*
		 * If the server claims to support oplock on this file, then we
		 * still need to check oplock even if the local_lease mount
		 * option is set, but there are servers which do not support
		 * oplock for which this mount option may be useful if the user
		 * knows that the file won't be changed on the server by anyone
		 * else.
		 */
		return generic_setlease(file, arg, lease, priv);
	else
		return -EAGAIN;
}

struct file_system_type cifs_fs_type = {
	.owner = THIS_MODULE,
	.name = "cifs",
	.init_fs_context = smb3_init_fs_context,
	.parameters = smb3_fs_parameters,
	.kill_sb = cifs_kill_sb,
	.fs_flags = FS_RENAME_DOES_D_MOVE,
};
MODULE_ALIAS_FS("cifs");

struct file_system_type smb3_fs_type = {
	.owner = THIS_MODULE,
	.name = "smb3",
	.init_fs_context = smb3_init_fs_context,
	.parameters = smb3_fs_parameters,
	.kill_sb = cifs_kill_sb,
	.fs_flags = FS_RENAME_DOES_D_MOVE,
};
MODULE_ALIAS_FS("smb3");
MODULE_ALIAS("smb3");

const struct inode_operations cifs_dir_inode_ops = {
	.create = cifs_create,
	.atomic_open = cifs_atomic_open,
	.lookup = cifs_lookup,
	.getattr = cifs_getattr,
	.unlink = cifs_unlink,
	.link = cifs_hardlink,
	.mkdir = cifs_mkdir,
	.rmdir = cifs_rmdir,
	.rename = cifs_rename2,
	.permission = cifs_permission,
	.setattr = cifs_setattr,
	.symlink = cifs_symlink,
	.mknod   = cifs_mknod,
	.listxattr = cifs_listxattr,
	.get_acl = cifs_get_acl,
	.set_acl = cifs_set_acl,
};

const struct inode_operations cifs_file_inode_ops = {
	.setattr = cifs_setattr,
	.getattr = cifs_getattr,
	.permission = cifs_permission,
	.listxattr = cifs_listxattr,
	.fiemap = cifs_fiemap,
	.get_acl = cifs_get_acl,
	.set_acl = cifs_set_acl,
};

const char *cifs_get_link(struct dentry *dentry, struct inode *inode,
			    struct delayed_call *done)
{
	char *target_path;

	target_path = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!target_path)
		return ERR_PTR(-ENOMEM);

	spin_lock(&inode->i_lock);
	if (likely(CIFS_I(inode)->symlink_target)) {
		strscpy(target_path, CIFS_I(inode)->symlink_target, PATH_MAX);
	} else {
		kfree(target_path);
		target_path = ERR_PTR(-EOPNOTSUPP);
	}
	spin_unlock(&inode->i_lock);

	if (!IS_ERR(target_path))
		set_delayed_call(done, kfree_link, target_path);

	return target_path;
}

const struct inode_operations cifs_symlink_inode_ops = {
	.get_link = cifs_get_link,
	.permission = cifs_permission,
	.listxattr = cifs_listxattr,
};

static loff_t cifs_remap_file_range(struct file *src_file, loff_t off,
		struct file *dst_file, loff_t destoff, loff_t len,
		unsigned int remap_flags)
{
	struct inode *src_inode = file_inode(src_file);
	struct inode *target_inode = file_inode(dst_file);
	struct cifsFileInfo *smb_file_src = src_file->private_data;
	struct cifsFileInfo *smb_file_target;
	struct cifs_tcon *target_tcon;
	unsigned int xid;
	int rc;

	if (remap_flags & ~(REMAP_FILE_DEDUP | REMAP_FILE_ADVISORY))
		return -EINVAL;

	cifs_dbg(FYI, "clone range\n");

	xid = get_xid();

	if (!src_file->private_data || !dst_file->private_data) {
		rc = -EBADF;
		cifs_dbg(VFS, "missing cifsFileInfo on copy range src file\n");
		goto out;
	}

	smb_file_target = dst_file->private_data;
	target_tcon = tlink_tcon(smb_file_target->tlink);

	/*
	 * Note: cifs case is easier than btrfs since server responsible for
	 * checks for proper open modes and file type and if it wants
	 * server could even support copy of range where source = target
	 */
	lock_two_nondirectories(target_inode, src_inode);

	if (len == 0)
		len = src_inode->i_size - off;

	cifs_dbg(FYI, "about to flush pages\n");
	/* should we flush first and last page first */
	truncate_inode_pages_range(&target_inode->i_data, destoff,
				   PAGE_ALIGN(destoff + len)-1);

	if (target_tcon->ses->server->ops->duplicate_extents)
		rc = target_tcon->ses->server->ops->duplicate_extents(xid,
			smb_file_src, smb_file_target, off, len, destoff);
	else
		rc = -EOPNOTSUPP;

	/* force revalidate of size and timestamps of target file now
	   that target is updated on the server */
	CIFS_I(target_inode)->time = 0;
	/* although unlocking in the reverse order from locking is not
	   strictly necessary here it is a little cleaner to be consistent */
	unlock_two_nondirectories(src_inode, target_inode);
out:
	free_xid(xid);
	return rc < 0 ? rc : len;
}

ssize_t cifs_file_copychunk_range(unsigned int xid,
				struct file *src_file, loff_t off,
				struct file *dst_file, loff_t destoff,
				size_t len, unsigned int flags)
{
	struct inode *src_inode = file_inode(src_file);
	struct inode *target_inode = file_inode(dst_file);
	struct cifsFileInfo *smb_file_src;
	struct cifsFileInfo *smb_file_target;
	struct cifs_tcon *src_tcon;
	struct cifs_tcon *target_tcon;
	ssize_t rc;

	cifs_dbg(FYI, "copychunk range\n");

	if (!src_file->private_data || !dst_file->private_data) {
		rc = -EBADF;
		cifs_dbg(VFS, "missing cifsFileInfo on copy range src file\n");
		goto out;
	}

	rc = -EXDEV;
	smb_file_target = dst_file->private_data;
	smb_file_src = src_file->private_data;
	src_tcon = tlink_tcon(smb_file_src->tlink);
	target_tcon = tlink_tcon(smb_file_target->tlink);

	if (src_tcon->ses != target_tcon->ses) {
		cifs_dbg(VFS, "source and target of copy not on same server\n");
		goto out;
	}

	rc = -EOPNOTSUPP;
	if (!target_tcon->ses->server->ops->copychunk_range)
		goto out;

	/*
	 * Note: cifs case is easier than btrfs since server responsible for
	 * checks for proper open modes and file type and if it wants
	 * server could even support copy of range where source = target
	 */
	lock_two_nondirectories(target_inode, src_inode);

	cifs_dbg(FYI, "about to flush pages\n");

	rc = filemap_write_and_wait_range(src_inode->i_mapping, off,
					  off + len - 1);
	if (rc)
		goto unlock;

	/* should we flush first and last page first */
	truncate_inode_pages(&target_inode->i_data, 0);

	rc = file_modified(dst_file);
	if (!rc)
		rc = target_tcon->ses->server->ops->copychunk_range(xid,
			smb_file_src, smb_file_target, off, len, destoff);

	file_accessed(src_file);

	/* force revalidate of size and timestamps of target file now
	 * that target is updated on the server
	 */
	CIFS_I(target_inode)->time = 0;

unlock:
	/* although unlocking in the reverse order from locking is not
	 * strictly necessary here it is a little cleaner to be consistent
	 */
	unlock_two_nondirectories(src_inode, target_inode);

out:
	return rc;
}

/*
 * Directory operations under CIFS/SMB2/SMB3 are synchronous, so fsync()
 * is a dummy operation.
 */
static int cifs_dir_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	cifs_dbg(FYI, "Sync directory - name: %pD datasync: 0x%x\n",
		 file, datasync);

	return 0;
}

static ssize_t cifs_copy_file_range(struct file *src_file, loff_t off,
				struct file *dst_file, loff_t destoff,
				size_t len, unsigned int flags)
{
	unsigned int xid = get_xid();
	ssize_t rc;
	struct cifsFileInfo *cfile = dst_file->private_data;

	if (cfile->swapfile) {
		rc = -EOPNOTSUPP;
		free_xid(xid);
		return rc;
	}

	rc = cifs_file_copychunk_range(xid, src_file, off, dst_file, destoff,
					len, flags);
	free_xid(xid);

	if (rc == -EOPNOTSUPP || rc == -EXDEV)
		rc = generic_copy_file_range(src_file, off, dst_file,
					     destoff, len, flags);
	return rc;
}

const struct file_operations cifs_file_ops = {
	.read_iter = cifs_loose_read_iter,
	.write_iter = cifs_file_write_iter,
	.open = cifs_open,
	.release = cifs_close,
	.lock = cifs_lock,
	.flock = cifs_flock,
	.fsync = cifs_fsync,
	.flush = cifs_flush,
	.mmap  = cifs_file_mmap,
	.splice_read = cifs_splice_read,
	.splice_write = iter_file_splice_write,
	.llseek = cifs_llseek,
	.unlocked_ioctl	= cifs_ioctl,
	.copy_file_range = cifs_copy_file_range,
	.remap_file_range = cifs_remap_file_range,
	.setlease = cifs_setlease,
	.fallocate = cifs_fallocate,
};

const struct file_operations cifs_file_strict_ops = {
	.read_iter = cifs_strict_readv,
	.write_iter = cifs_strict_writev,
	.open = cifs_open,
	.release = cifs_close,
	.lock = cifs_lock,
	.flock = cifs_flock,
	.fsync = cifs_strict_fsync,
	.flush = cifs_flush,
	.mmap = cifs_file_strict_mmap,
	.splice_read = cifs_splice_read,
	.splice_write = iter_file_splice_write,
	.llseek = cifs_llseek,
	.unlocked_ioctl	= cifs_ioctl,
	.copy_file_range = cifs_copy_file_range,
	.remap_file_range = cifs_remap_file_range,
	.setlease = cifs_setlease,
	.fallocate = cifs_fallocate,
};

const struct file_operations cifs_file_direct_ops = {
	.read_iter = cifs_direct_readv,
	.write_iter = cifs_direct_writev,
	.open = cifs_open,
	.release = cifs_close,
	.lock = cifs_lock,
	.flock = cifs_flock,
	.fsync = cifs_fsync,
	.flush = cifs_flush,
	.mmap = cifs_file_mmap,
	.splice_read = direct_splice_read,
	.splice_write = iter_file_splice_write,
	.unlocked_ioctl  = cifs_ioctl,
	.copy_file_range = cifs_copy_file_range,
	.remap_file_range = cifs_remap_file_range,
	.llseek = cifs_llseek,
	.setlease = cifs_setlease,
	.fallocate = cifs_fallocate,
};

const struct file_operations cifs_file_nobrl_ops = {
	.read_iter = cifs_loose_read_iter,
	.write_iter = cifs_file_write_iter,
	.open = cifs_open,
	.release = cifs_close,
	.fsync = cifs_fsync,
	.flush = cifs_flush,
	.mmap  = cifs_file_mmap,
	.splice_read = cifs_splice_read,
	.splice_write = iter_file_splice_write,
	.llseek = cifs_llseek,
	.unlocked_ioctl	= cifs_ioctl,
	.copy_file_range = cifs_copy_file_range,
	.remap_file_range = cifs_remap_file_range,
	.setlease = cifs_setlease,
	.fallocate = cifs_fallocate,
};

const struct file_operations cifs_file_strict_nobrl_ops = {
	.read_iter = cifs_strict_readv,
	.write_iter = cifs_strict_writev,
	.open = cifs_open,
	.release = cifs_close,
	.fsync = cifs_strict_fsync,
	.flush = cifs_flush,
	.mmap = cifs_file_strict_mmap,
	.splice_read = cifs_splice_read,
	.splice_write = iter_file_splice_write,
	.llseek = cifs_llseek,
	.unlocked_ioctl	= cifs_ioctl,
	.copy_file_range = cifs_copy_file_range,
	.remap_file_range = cifs_remap_file_range,
	.setlease = cifs_setlease,
	.fallocate = cifs_fallocate,
};

const struct file_operations cifs_file_direct_nobrl_ops = {
	.read_iter = cifs_direct_readv,
	.write_iter = cifs_direct_writev,
	.open = cifs_open,
	.release = cifs_close,
	.fsync = cifs_fsync,
	.flush = cifs_flush,
	.mmap = cifs_file_mmap,
	.splice_read = direct_splice_read,
	.splice_write = iter_file_splice_write,
	.unlocked_ioctl  = cifs_ioctl,
	.copy_file_range = cifs_copy_file_range,
	.remap_file_range = cifs_remap_file_range,
	.llseek = cifs_llseek,
	.setlease = cifs_setlease,
	.fallocate = cifs_fallocate,
};

const struct file_operations cifs_dir_ops = {
	.iterate_shared = cifs_readdir,
	.release = cifs_closedir,
	.read    = generic_read_dir,
	.unlocked_ioctl  = cifs_ioctl,
	.copy_file_range = cifs_copy_file_range,
	.remap_file_range = cifs_remap_file_range,
	.llseek = generic_file_llseek,
	.fsync = cifs_dir_fsync,
};

static void
cifs_init_once(void *inode)
{
	struct cifsInodeInfo *cifsi = inode;

	inode_init_once(&cifsi->netfs.inode);
	init_rwsem(&cifsi->lock_sem);
}

static int __init
cifs_init_inodecache(void)
{
	cifs_inode_cachep = kmem_cache_create("cifs_inode_cache",
					      sizeof(struct cifsInodeInfo),
					      0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD|SLAB_ACCOUNT),
					      cifs_init_once);
	if (cifs_inode_cachep == NULL)
		return -ENOMEM;

	return 0;
}

static void
cifs_destroy_inodecache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(cifs_inode_cachep);
}

static int
cifs_init_request_bufs(void)
{
	/*
	 * SMB2 maximum header size is bigger than CIFS one - no problems to
	 * allocate some more bytes for CIFS.
	 */
	size_t max_hdr_size = MAX_SMB2_HDR_SIZE;

	if (CIFSMaxBufSize < 8192) {
	/* Buffer size can not be smaller than 2 * PATH_MAX since maximum
	Unicode path name has to fit in any SMB/CIFS path based frames */
		CIFSMaxBufSize = 8192;
	} else if (CIFSMaxBufSize > 1024*127) {
		CIFSMaxBufSize = 1024 * 127;
	} else {
		CIFSMaxBufSize &= 0x1FE00; /* Round size to even 512 byte mult*/
	}
/*
	cifs_dbg(VFS, "CIFSMaxBufSize %d 0x%x\n",
		 CIFSMaxBufSize, CIFSMaxBufSize);
*/
	cifs_req_cachep = kmem_cache_create_usercopy("cifs_request",
					    CIFSMaxBufSize + max_hdr_size, 0,
					    SLAB_HWCACHE_ALIGN, 0,
					    CIFSMaxBufSize + max_hdr_size,
					    NULL);
	if (cifs_req_cachep == NULL)
		return -ENOMEM;

	if (cifs_min_rcv < 1)
		cifs_min_rcv = 1;
	else if (cifs_min_rcv > 64) {
		cifs_min_rcv = 64;
		cifs_dbg(VFS, "cifs_min_rcv set to maximum (64)\n");
	}

	cifs_req_poolp = mempool_create_slab_pool(cifs_min_rcv,
						  cifs_req_cachep);

	if (cifs_req_poolp == NULL) {
		kmem_cache_destroy(cifs_req_cachep);
		return -ENOMEM;
	}
	/* MAX_CIFS_SMALL_BUFFER_SIZE bytes is enough for most SMB responses and
	almost all handle based requests (but not write response, nor is it
	sufficient for path based requests).  A smaller size would have
	been more efficient (compacting multiple slab items on one 4k page)
	for the case in which debug was on, but this larger size allows
	more SMBs to use small buffer alloc and is still much more
	efficient to alloc 1 per page off the slab compared to 17K (5page)
	alloc of large cifs buffers even when page debugging is on */
	cifs_sm_req_cachep = kmem_cache_create_usercopy("cifs_small_rq",
			MAX_CIFS_SMALL_BUFFER_SIZE, 0, SLAB_HWCACHE_ALIGN,
			0, MAX_CIFS_SMALL_BUFFER_SIZE, NULL);
	if (cifs_sm_req_cachep == NULL) {
		mempool_destroy(cifs_req_poolp);
		kmem_cache_destroy(cifs_req_cachep);
		return -ENOMEM;
	}

	if (cifs_min_small < 2)
		cifs_min_small = 2;
	else if (cifs_min_small > 256) {
		cifs_min_small = 256;
		cifs_dbg(FYI, "cifs_min_small set to maximum (256)\n");
	}

	cifs_sm_req_poolp = mempool_create_slab_pool(cifs_min_small,
						     cifs_sm_req_cachep);

	if (cifs_sm_req_poolp == NULL) {
		mempool_destroy(cifs_req_poolp);
		kmem_cache_destroy(cifs_req_cachep);
		kmem_cache_destroy(cifs_sm_req_cachep);
		return -ENOMEM;
	}

	return 0;
}

static void
cifs_destroy_request_bufs(void)
{
	mempool_destroy(cifs_req_poolp);
	kmem_cache_destroy(cifs_req_cachep);
	mempool_destroy(cifs_sm_req_poolp);
	kmem_cache_destroy(cifs_sm_req_cachep);
}

static int init_mids(void)
{
	cifs_mid_cachep = kmem_cache_create("cifs_mpx_ids",
					    sizeof(struct mid_q_entry), 0,
					    SLAB_HWCACHE_ALIGN, NULL);
	if (cifs_mid_cachep == NULL)
		return -ENOMEM;

	/* 3 is a reasonable minimum number of simultaneous operations */
	cifs_mid_poolp = mempool_create_slab_pool(3, cifs_mid_cachep);
	if (cifs_mid_poolp == NULL) {
		kmem_cache_destroy(cifs_mid_cachep);
		return -ENOMEM;
	}

	return 0;
}

static void destroy_mids(void)
{
	mempool_destroy(cifs_mid_poolp);
	kmem_cache_destroy(cifs_mid_cachep);
}

static int __init
init_cifs(void)
{
	int rc = 0;
	cifs_proc_init();
	INIT_LIST_HEAD(&cifs_tcp_ses_list);
/*
 *  Initialize Global counters
 */
	atomic_set(&sesInfoAllocCount, 0);
	atomic_set(&tconInfoAllocCount, 0);
	atomic_set(&tcpSesNextId, 0);
	atomic_set(&tcpSesAllocCount, 0);
	atomic_set(&tcpSesReconnectCount, 0);
	atomic_set(&tconInfoReconnectCount, 0);

	atomic_set(&buf_alloc_count, 0);
	atomic_set(&small_buf_alloc_count, 0);
#ifdef CONFIG_CIFS_STATS2
	atomic_set(&total_buf_alloc_count, 0);
	atomic_set(&total_small_buf_alloc_count, 0);
	if (slow_rsp_threshold < 1)
		cifs_dbg(FYI, "slow_response_threshold msgs disabled\n");
	else if (slow_rsp_threshold > 32767)
		cifs_dbg(VFS,
		       "slow response threshold set higher than recommended (0 to 32767)\n");
#endif /* CONFIG_CIFS_STATS2 */

	atomic_set(&mid_count, 0);
	GlobalCurrentXid = 0;
	GlobalTotalActiveXid = 0;
	GlobalMaxActiveXid = 0;
	spin_lock_init(&cifs_tcp_ses_lock);
	spin_lock_init(&GlobalMid_Lock);

	cifs_lock_secret = get_random_u32();

	if (cifs_max_pending < 2) {
		cifs_max_pending = 2;
		cifs_dbg(FYI, "cifs_max_pending set to min of 2\n");
	} else if (cifs_max_pending > CIFS_MAX_REQ) {
		cifs_max_pending = CIFS_MAX_REQ;
		cifs_dbg(FYI, "cifs_max_pending set to max of %u\n",
			 CIFS_MAX_REQ);
	}

	cifsiod_wq = alloc_workqueue("cifsiod", WQ_FREEZABLE|WQ_MEM_RECLAIM, 0);
	if (!cifsiod_wq) {
		rc = -ENOMEM;
		goto out_clean_proc;
	}

	/*
	 * Consider in future setting limit!=0 maybe to min(num_of_cores - 1, 3)
	 * so that we don't launch too many worker threads but
	 * Documentation/core-api/workqueue.rst recommends setting it to 0
	 */

	/* WQ_UNBOUND allows decrypt tasks to run on any CPU */
	decrypt_wq = alloc_workqueue("smb3decryptd",
				     WQ_UNBOUND|WQ_FREEZABLE|WQ_MEM_RECLAIM, 0);
	if (!decrypt_wq) {
		rc = -ENOMEM;
		goto out_destroy_cifsiod_wq;
	}

	fileinfo_put_wq = alloc_workqueue("cifsfileinfoput",
				     WQ_UNBOUND|WQ_FREEZABLE|WQ_MEM_RECLAIM, 0);
	if (!fileinfo_put_wq) {
		rc = -ENOMEM;
		goto out_destroy_decrypt_wq;
	}

	cifsoplockd_wq = alloc_workqueue("cifsoplockd",
					 WQ_FREEZABLE|WQ_MEM_RECLAIM, 0);
	if (!cifsoplockd_wq) {
		rc = -ENOMEM;
		goto out_destroy_fileinfo_put_wq;
	}

	deferredclose_wq = alloc_workqueue("deferredclose",
					   WQ_FREEZABLE|WQ_MEM_RECLAIM, 0);
	if (!deferredclose_wq) {
		rc = -ENOMEM;
		goto out_destroy_cifsoplockd_wq;
	}

	rc = cifs_init_inodecache();
	if (rc)
		goto out_destroy_deferredclose_wq;

	rc = init_mids();
	if (rc)
		goto out_destroy_inodecache;

	rc = cifs_init_request_bufs();
	if (rc)
		goto out_destroy_mids;

#ifdef CONFIG_CIFS_DFS_UPCALL
	rc = dfs_cache_init();
	if (rc)
		goto out_destroy_request_bufs;
#endif /* CONFIG_CIFS_DFS_UPCALL */
#ifdef CONFIG_CIFS_UPCALL
	rc = init_cifs_spnego();
	if (rc)
		goto out_destroy_dfs_cache;
#endif /* CONFIG_CIFS_UPCALL */
#ifdef CONFIG_CIFS_SWN_UPCALL
	rc = cifs_genl_init();
	if (rc)
		goto out_register_key_type;
#endif /* CONFIG_CIFS_SWN_UPCALL */

	rc = init_cifs_idmap();
	if (rc)
		goto out_cifs_swn_init;

	rc = register_filesystem(&cifs_fs_type);
	if (rc)
		goto out_init_cifs_idmap;

	rc = register_filesystem(&smb3_fs_type);
	if (rc) {
		unregister_filesystem(&cifs_fs_type);
		goto out_init_cifs_idmap;
	}

	return 0;

out_init_cifs_idmap:
	exit_cifs_idmap();
out_cifs_swn_init:
#ifdef CONFIG_CIFS_SWN_UPCALL
	cifs_genl_exit();
out_register_key_type:
#endif
#ifdef CONFIG_CIFS_UPCALL
	exit_cifs_spnego();
out_destroy_dfs_cache:
#endif
#ifdef CONFIG_CIFS_DFS_UPCALL
	dfs_cache_destroy();
out_destroy_request_bufs:
#endif
	cifs_destroy_request_bufs();
out_destroy_mids:
	destroy_mids();
out_destroy_inodecache:
	cifs_destroy_inodecache();
out_destroy_deferredclose_wq:
	destroy_workqueue(deferredclose_wq);
out_destroy_cifsoplockd_wq:
	destroy_workqueue(cifsoplockd_wq);
out_destroy_fileinfo_put_wq:
	destroy_workqueue(fileinfo_put_wq);
out_destroy_decrypt_wq:
	destroy_workqueue(decrypt_wq);
out_destroy_cifsiod_wq:
	destroy_workqueue(cifsiod_wq);
out_clean_proc:
	cifs_proc_clean();
	return rc;
}

static void __exit
exit_cifs(void)
{
	cifs_dbg(NOISY, "exit_smb3\n");
	unregister_filesystem(&cifs_fs_type);
	unregister_filesystem(&smb3_fs_type);
	cifs_dfs_release_automount_timer();
	exit_cifs_idmap();
#ifdef CONFIG_CIFS_SWN_UPCALL
	cifs_genl_exit();
#endif
#ifdef CONFIG_CIFS_UPCALL
	exit_cifs_spnego();
#endif
#ifdef CONFIG_CIFS_DFS_UPCALL
	dfs_cache_destroy();
#endif
	cifs_destroy_request_bufs();
	destroy_mids();
	cifs_destroy_inodecache();
	destroy_workqueue(deferredclose_wq);
	destroy_workqueue(cifsoplockd_wq);
	destroy_workqueue(decrypt_wq);
	destroy_workqueue(fileinfo_put_wq);
	destroy_workqueue(cifsiod_wq);
	cifs_proc_clean();
}

MODULE_AUTHOR("Steve French");
MODULE_LICENSE("GPL");	/* combination of LGPL + GPL source behaves as GPL */
MODULE_DESCRIPTION
	("VFS to access SMB3 servers e.g. Samba, Macs, Azure and Windows (and "
	"also older servers complying with the SNIA CIFS Specification)");
MODULE_VERSION(CIFS_VERSION);
MODULE_SOFTDEP("ecb");
MODULE_SOFTDEP("hmac");
MODULE_SOFTDEP("md5");
MODULE_SOFTDEP("nls");
MODULE_SOFTDEP("aes");
MODULE_SOFTDEP("cmac");
MODULE_SOFTDEP("sha256");
MODULE_SOFTDEP("sha512");
MODULE_SOFTDEP("aead2");
MODULE_SOFTDEP("ccm");
MODULE_SOFTDEP("gcm");
module_init(init_cifs)
module_exit(exit_cifs)
