/*
 *  linux/fs/nfs/super.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  nfs superblock handling functions
 *
 *  Modularised by Alan Cox <alan@lxorguk.ukuu.org.uk>, while hacking some
 *  experimental NFS changes. Modularisation taken straight from SYS5 fs.
 *
 *  Change to nfs_read_super() to permit NFS mounts to multi-homed hosts.
 *  J.S.Peatfield@damtp.cam.ac.uk
 *
 *  Split from inode.c by David Howells <dhowells@redhat.com>
 *
 * - superblocks are indexed on server only - all inodes, dentries, etc. associated with a
 *   particular server are held in the same superblock
 * - NFS superblocks can have several effective roots to the dentry tree
 * - directory type roots are spliced into the tree when a path from one root reaches the root
 *   of another (see nfs_lookup())
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/addr.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/metrics.h>
#include <linux/sunrpc/xprtsock.h>
#include <linux/sunrpc/xprtrdma.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs4_mount.h>
#include <linux/lockd/bind.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/vfs.h>
#include <linux/inet.h>
#include <linux/in6.h>
#include <linux/slab.h>
#include <net/ipv6.h>
#include <linux/netdevice.h>
#include <linux/nfs_xdr.h>
#include <linux/magic.h>
#include <linux/parser.h>
#include <linux/nsproxy.h>
#include <linux/rcupdate.h>

#include <linux/uaccess.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"
#include "iostat.h"
#include "internal.h"
#include "fscache.h"
#include "nfs4session.h"
#include "pnfs.h"
#include "nfs.h"

#define NFSDBG_FACILITY		NFSDBG_VFS
#define NFS_TEXT_DATA		1

#if IS_ENABLED(CONFIG_NFS_V3)
#define NFS_DEFAULT_VERSION 3
#else
#define NFS_DEFAULT_VERSION 2
#endif

enum {
	/* Mount options that take no arguments */
	Opt_soft, Opt_hard,
	Opt_posix, Opt_noposix,
	Opt_cto, Opt_nocto,
	Opt_ac, Opt_noac,
	Opt_lock, Opt_nolock,
	Opt_udp, Opt_tcp, Opt_rdma,
	Opt_acl, Opt_noacl,
	Opt_rdirplus, Opt_nordirplus,
	Opt_sharecache, Opt_nosharecache,
	Opt_resvport, Opt_noresvport,
	Opt_fscache, Opt_nofscache,
	Opt_migration, Opt_nomigration,

	/* Mount options that take integer arguments */
	Opt_port,
	Opt_rsize, Opt_wsize, Opt_bsize,
	Opt_timeo, Opt_retrans,
	Opt_acregmin, Opt_acregmax,
	Opt_acdirmin, Opt_acdirmax,
	Opt_actimeo,
	Opt_namelen,
	Opt_mountport,
	Opt_mountvers,
	Opt_minorversion,

	/* Mount options that take string arguments */
	Opt_nfsvers,
	Opt_sec, Opt_proto, Opt_mountproto, Opt_mounthost,
	Opt_addr, Opt_mountaddr, Opt_clientaddr,
	Opt_lookupcache,
	Opt_fscache_uniq,
	Opt_local_lock,

	/* Special mount options */
	Opt_userspace, Opt_deprecated, Opt_sloppy,

	Opt_err
};

static const match_table_t nfs_mount_option_tokens = {
	{ Opt_userspace, "bg" },
	{ Opt_userspace, "fg" },
	{ Opt_userspace, "retry=%s" },

	{ Opt_sloppy, "sloppy" },

	{ Opt_soft, "soft" },
	{ Opt_hard, "hard" },
	{ Opt_deprecated, "intr" },
	{ Opt_deprecated, "nointr" },
	{ Opt_posix, "posix" },
	{ Opt_noposix, "noposix" },
	{ Opt_cto, "cto" },
	{ Opt_nocto, "nocto" },
	{ Opt_ac, "ac" },
	{ Opt_noac, "noac" },
	{ Opt_lock, "lock" },
	{ Opt_nolock, "nolock" },
	{ Opt_udp, "udp" },
	{ Opt_tcp, "tcp" },
	{ Opt_rdma, "rdma" },
	{ Opt_acl, "acl" },
	{ Opt_noacl, "noacl" },
	{ Opt_rdirplus, "rdirplus" },
	{ Opt_nordirplus, "nordirplus" },
	{ Opt_sharecache, "sharecache" },
	{ Opt_nosharecache, "nosharecache" },
	{ Opt_resvport, "resvport" },
	{ Opt_noresvport, "noresvport" },
	{ Opt_fscache, "fsc" },
	{ Opt_nofscache, "nofsc" },
	{ Opt_migration, "migration" },
	{ Opt_nomigration, "nomigration" },

	{ Opt_port, "port=%s" },
	{ Opt_rsize, "rsize=%s" },
	{ Opt_wsize, "wsize=%s" },
	{ Opt_bsize, "bsize=%s" },
	{ Opt_timeo, "timeo=%s" },
	{ Opt_retrans, "retrans=%s" },
	{ Opt_acregmin, "acregmin=%s" },
	{ Opt_acregmax, "acregmax=%s" },
	{ Opt_acdirmin, "acdirmin=%s" },
	{ Opt_acdirmax, "acdirmax=%s" },
	{ Opt_actimeo, "actimeo=%s" },
	{ Opt_namelen, "namlen=%s" },
	{ Opt_mountport, "mountport=%s" },
	{ Opt_mountvers, "mountvers=%s" },
	{ Opt_minorversion, "minorversion=%s" },

	{ Opt_nfsvers, "nfsvers=%s" },
	{ Opt_nfsvers, "vers=%s" },

	{ Opt_sec, "sec=%s" },
	{ Opt_proto, "proto=%s" },
	{ Opt_mountproto, "mountproto=%s" },
	{ Opt_addr, "addr=%s" },
	{ Opt_clientaddr, "clientaddr=%s" },
	{ Opt_mounthost, "mounthost=%s" },
	{ Opt_mountaddr, "mountaddr=%s" },

	{ Opt_lookupcache, "lookupcache=%s" },
	{ Opt_fscache_uniq, "fsc=%s" },
	{ Opt_local_lock, "local_lock=%s" },

	/* The following needs to be listed after all other options */
	{ Opt_nfsvers, "v%s" },

	{ Opt_err, NULL }
};

enum {
	Opt_xprt_udp, Opt_xprt_udp6, Opt_xprt_tcp, Opt_xprt_tcp6, Opt_xprt_rdma,
	Opt_xprt_rdma6,

	Opt_xprt_err
};

static const match_table_t nfs_xprt_protocol_tokens = {
	{ Opt_xprt_udp, "udp" },
	{ Opt_xprt_udp6, "udp6" },
	{ Opt_xprt_tcp, "tcp" },
	{ Opt_xprt_tcp6, "tcp6" },
	{ Opt_xprt_rdma, "rdma" },
	{ Opt_xprt_rdma6, "rdma6" },

	{ Opt_xprt_err, NULL }
};

enum {
	Opt_sec_none, Opt_sec_sys,
	Opt_sec_krb5, Opt_sec_krb5i, Opt_sec_krb5p,
	Opt_sec_lkey, Opt_sec_lkeyi, Opt_sec_lkeyp,
	Opt_sec_spkm, Opt_sec_spkmi, Opt_sec_spkmp,

	Opt_sec_err
};

static const match_table_t nfs_secflavor_tokens = {
	{ Opt_sec_none, "none" },
	{ Opt_sec_none, "null" },
	{ Opt_sec_sys, "sys" },

	{ Opt_sec_krb5, "krb5" },
	{ Opt_sec_krb5i, "krb5i" },
	{ Opt_sec_krb5p, "krb5p" },

	{ Opt_sec_lkey, "lkey" },
	{ Opt_sec_lkeyi, "lkeyi" },
	{ Opt_sec_lkeyp, "lkeyp" },

	{ Opt_sec_spkm, "spkm3" },
	{ Opt_sec_spkmi, "spkm3i" },
	{ Opt_sec_spkmp, "spkm3p" },

	{ Opt_sec_err, NULL }
};

enum {
	Opt_lookupcache_all, Opt_lookupcache_positive,
	Opt_lookupcache_none,

	Opt_lookupcache_err
};

static match_table_t nfs_lookupcache_tokens = {
	{ Opt_lookupcache_all, "all" },
	{ Opt_lookupcache_positive, "pos" },
	{ Opt_lookupcache_positive, "positive" },
	{ Opt_lookupcache_none, "none" },

	{ Opt_lookupcache_err, NULL }
};

enum {
	Opt_local_lock_all, Opt_local_lock_flock, Opt_local_lock_posix,
	Opt_local_lock_none,

	Opt_local_lock_err
};

static match_table_t nfs_local_lock_tokens = {
	{ Opt_local_lock_all, "all" },
	{ Opt_local_lock_flock, "flock" },
	{ Opt_local_lock_posix, "posix" },
	{ Opt_local_lock_none, "none" },

	{ Opt_local_lock_err, NULL }
};

enum {
	Opt_vers_2, Opt_vers_3, Opt_vers_4, Opt_vers_4_0,
	Opt_vers_4_1, Opt_vers_4_2,

	Opt_vers_err
};

static match_table_t nfs_vers_tokens = {
	{ Opt_vers_2, "2" },
	{ Opt_vers_3, "3" },
	{ Opt_vers_4, "4" },
	{ Opt_vers_4_0, "4.0" },
	{ Opt_vers_4_1, "4.1" },
	{ Opt_vers_4_2, "4.2" },

	{ Opt_vers_err, NULL }
};

static struct dentry *nfs_xdev_mount(struct file_system_type *fs_type,
		int flags, const char *dev_name, void *raw_data);

struct file_system_type nfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs",
	.mount		= nfs_fs_mount,
	.kill_sb	= nfs_kill_super,
	.fs_flags	= FS_RENAME_DOES_D_MOVE|FS_BINARY_MOUNTDATA,
};
MODULE_ALIAS_FS("nfs");
EXPORT_SYMBOL_GPL(nfs_fs_type);

struct file_system_type nfs_xdev_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs",
	.mount		= nfs_xdev_mount,
	.kill_sb	= nfs_kill_super,
	.fs_flags	= FS_RENAME_DOES_D_MOVE|FS_BINARY_MOUNTDATA,
};

const struct super_operations nfs_sops = {
	.alloc_inode	= nfs_alloc_inode,
	.destroy_inode	= nfs_destroy_inode,
	.write_inode	= nfs_write_inode,
	.drop_inode	= nfs_drop_inode,
	.statfs		= nfs_statfs,
	.evict_inode	= nfs_evict_inode,
	.umount_begin	= nfs_umount_begin,
	.show_options	= nfs_show_options,
	.show_devname	= nfs_show_devname,
	.show_path	= nfs_show_path,
	.show_stats	= nfs_show_stats,
	.remount_fs	= nfs_remount,
};
EXPORT_SYMBOL_GPL(nfs_sops);

#if IS_ENABLED(CONFIG_NFS_V4)
static void nfs4_validate_mount_flags(struct nfs_parsed_mount_data *);
static int nfs4_validate_mount_data(void *options,
	struct nfs_parsed_mount_data *args, const char *dev_name);

struct file_system_type nfs4_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs4",
	.mount		= nfs_fs_mount,
	.kill_sb	= nfs_kill_super,
	.fs_flags	= FS_RENAME_DOES_D_MOVE|FS_BINARY_MOUNTDATA,
};
MODULE_ALIAS_FS("nfs4");
MODULE_ALIAS("nfs4");
EXPORT_SYMBOL_GPL(nfs4_fs_type);

static int __init register_nfs4_fs(void)
{
	return register_filesystem(&nfs4_fs_type);
}

static void unregister_nfs4_fs(void)
{
	unregister_filesystem(&nfs4_fs_type);
}
#else
static int __init register_nfs4_fs(void)
{
	return 0;
}

static void unregister_nfs4_fs(void)
{
}
#endif

static struct shrinker acl_shrinker = {
	.count_objects	= nfs_access_cache_count,
	.scan_objects	= nfs_access_cache_scan,
	.seeks		= DEFAULT_SEEKS,
};

/*
 * Register the NFS filesystems
 */
int __init register_nfs_fs(void)
{
	int ret;

        ret = register_filesystem(&nfs_fs_type);
	if (ret < 0)
		goto error_0;

	ret = register_nfs4_fs();
	if (ret < 0)
		goto error_1;

	ret = nfs_register_sysctl();
	if (ret < 0)
		goto error_2;
	ret = register_shrinker(&acl_shrinker);
	if (ret < 0)
		goto error_3;
	return 0;
error_3:
	nfs_unregister_sysctl();
error_2:
	unregister_nfs4_fs();
error_1:
	unregister_filesystem(&nfs_fs_type);
error_0:
	return ret;
}

/*
 * Unregister the NFS filesystems
 */
void __exit unregister_nfs_fs(void)
{
	unregister_shrinker(&acl_shrinker);
	nfs_unregister_sysctl();
	unregister_nfs4_fs();
	unregister_filesystem(&nfs_fs_type);
}

bool nfs_sb_active(struct super_block *sb)
{
	struct nfs_server *server = NFS_SB(sb);

	if (!atomic_inc_not_zero(&sb->s_active))
		return false;
	if (atomic_inc_return(&server->active) != 1)
		atomic_dec(&sb->s_active);
	return true;
}
EXPORT_SYMBOL_GPL(nfs_sb_active);

void nfs_sb_deactive(struct super_block *sb)
{
	struct nfs_server *server = NFS_SB(sb);

	if (atomic_dec_and_test(&server->active))
		deactivate_super(sb);
}
EXPORT_SYMBOL_GPL(nfs_sb_deactive);

/*
 * Deliver file system statistics to userspace
 */
int nfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct nfs_server *server = NFS_SB(dentry->d_sb);
	unsigned char blockbits;
	unsigned long blockres;
	struct nfs_fh *fh = NFS_FH(d_inode(dentry));
	struct nfs_fsstat res;
	int error = -ENOMEM;

	res.fattr = nfs_alloc_fattr();
	if (res.fattr == NULL)
		goto out_err;

	error = server->nfs_client->rpc_ops->statfs(server, fh, &res);
	if (unlikely(error == -ESTALE)) {
		struct dentry *pd_dentry;

		pd_dentry = dget_parent(dentry);
		if (pd_dentry != NULL) {
			nfs_zap_caches(d_inode(pd_dentry));
			dput(pd_dentry);
		}
	}
	nfs_free_fattr(res.fattr);
	if (error < 0)
		goto out_err;

	buf->f_type = NFS_SUPER_MAGIC;

	/*
	 * Current versions of glibc do not correctly handle the
	 * case where f_frsize != f_bsize.  Eventually we want to
	 * report the value of wtmult in this field.
	 */
	buf->f_frsize = dentry->d_sb->s_blocksize;

	/*
	 * On most *nix systems, f_blocks, f_bfree, and f_bavail
	 * are reported in units of f_frsize.  Linux hasn't had
	 * an f_frsize field in its statfs struct until recently,
	 * thus historically Linux's sys_statfs reports these
	 * fields in units of f_bsize.
	 */
	buf->f_bsize = dentry->d_sb->s_blocksize;
	blockbits = dentry->d_sb->s_blocksize_bits;
	blockres = (1 << blockbits) - 1;
	buf->f_blocks = (res.tbytes + blockres) >> blockbits;
	buf->f_bfree = (res.fbytes + blockres) >> blockbits;
	buf->f_bavail = (res.abytes + blockres) >> blockbits;

	buf->f_files = res.tfiles;
	buf->f_ffree = res.afiles;

	buf->f_namelen = server->namelen;

	return 0;

 out_err:
	dprintk("%s: statfs error = %d\n", __func__, -error);
	return error;
}
EXPORT_SYMBOL_GPL(nfs_statfs);

/*
 * Map the security flavour number to a name
 */
static const char *nfs_pseudoflavour_to_name(rpc_authflavor_t flavour)
{
	static const struct {
		rpc_authflavor_t flavour;
		const char *str;
	} sec_flavours[NFS_AUTH_INFO_MAX_FLAVORS] = {
		/* update NFS_AUTH_INFO_MAX_FLAVORS when this list changes! */
		{ RPC_AUTH_NULL, "null" },
		{ RPC_AUTH_UNIX, "sys" },
		{ RPC_AUTH_GSS_KRB5, "krb5" },
		{ RPC_AUTH_GSS_KRB5I, "krb5i" },
		{ RPC_AUTH_GSS_KRB5P, "krb5p" },
		{ RPC_AUTH_GSS_LKEY, "lkey" },
		{ RPC_AUTH_GSS_LKEYI, "lkeyi" },
		{ RPC_AUTH_GSS_LKEYP, "lkeyp" },
		{ RPC_AUTH_GSS_SPKM, "spkm" },
		{ RPC_AUTH_GSS_SPKMI, "spkmi" },
		{ RPC_AUTH_GSS_SPKMP, "spkmp" },
		{ UINT_MAX, "unknown" }
	};
	int i;

	for (i = 0; sec_flavours[i].flavour != UINT_MAX; i++) {
		if (sec_flavours[i].flavour == flavour)
			break;
	}
	return sec_flavours[i].str;
}

static void nfs_show_mountd_netid(struct seq_file *m, struct nfs_server *nfss,
				  int showdefaults)
{
	struct sockaddr *sap = (struct sockaddr *) &nfss->mountd_address;
	char *proto = NULL;

	switch (sap->sa_family) {
	case AF_INET:
		switch (nfss->mountd_protocol) {
		case IPPROTO_UDP:
			proto = RPCBIND_NETID_UDP;
			break;
		case IPPROTO_TCP:
			proto = RPCBIND_NETID_TCP;
			break;
		}
		break;
	case AF_INET6:
		switch (nfss->mountd_protocol) {
		case IPPROTO_UDP:
			proto = RPCBIND_NETID_UDP6;
			break;
		case IPPROTO_TCP:
			proto = RPCBIND_NETID_TCP6;
			break;
		}
		break;
	}
	if (proto || showdefaults)
		seq_printf(m, ",mountproto=%s", proto ?: "auto");
}

static void nfs_show_mountd_options(struct seq_file *m, struct nfs_server *nfss,
				    int showdefaults)
{
	struct sockaddr *sap = (struct sockaddr *)&nfss->mountd_address;

	if (nfss->flags & NFS_MOUNT_LEGACY_INTERFACE)
		return;

	switch (sap->sa_family) {
	case AF_INET: {
		struct sockaddr_in *sin = (struct sockaddr_in *)sap;
		seq_printf(m, ",mountaddr=%pI4", &sin->sin_addr.s_addr);
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sap;
		seq_printf(m, ",mountaddr=%pI6c", &sin6->sin6_addr);
		break;
	}
	default:
		if (showdefaults)
			seq_printf(m, ",mountaddr=unspecified");
	}

	if (nfss->mountd_version || showdefaults)
		seq_printf(m, ",mountvers=%u", nfss->mountd_version);
	if ((nfss->mountd_port &&
		nfss->mountd_port != (unsigned short)NFS_UNSPEC_PORT) ||
		showdefaults)
		seq_printf(m, ",mountport=%u", nfss->mountd_port);

	nfs_show_mountd_netid(m, nfss, showdefaults);
}

#if IS_ENABLED(CONFIG_NFS_V4)
static void nfs_show_nfsv4_options(struct seq_file *m, struct nfs_server *nfss,
				    int showdefaults)
{
	struct nfs_client *clp = nfss->nfs_client;

	seq_printf(m, ",clientaddr=%s", clp->cl_ipaddr);
}
#else
static void nfs_show_nfsv4_options(struct seq_file *m, struct nfs_server *nfss,
				    int showdefaults)
{
}
#endif

static void nfs_show_nfs_version(struct seq_file *m,
		unsigned int version,
		unsigned int minorversion)
{
	seq_printf(m, ",vers=%u", version);
	if (version == 4)
		seq_printf(m, ".%u", minorversion);
}

/*
 * Describe the mount options in force on this server representation
 */
static void nfs_show_mount_options(struct seq_file *m, struct nfs_server *nfss,
				   int showdefaults)
{
	static const struct proc_nfs_info {
		int flag;
		const char *str;
		const char *nostr;
	} nfs_info[] = {
		{ NFS_MOUNT_SOFT, ",soft", ",hard" },
		{ NFS_MOUNT_POSIX, ",posix", "" },
		{ NFS_MOUNT_NOCTO, ",nocto", "" },
		{ NFS_MOUNT_NOAC, ",noac", "" },
		{ NFS_MOUNT_NONLM, ",nolock", "" },
		{ NFS_MOUNT_NOACL, ",noacl", "" },
		{ NFS_MOUNT_NORDIRPLUS, ",nordirplus", "" },
		{ NFS_MOUNT_UNSHARED, ",nosharecache", "" },
		{ NFS_MOUNT_NORESVPORT, ",noresvport", "" },
		{ 0, NULL, NULL }
	};
	const struct proc_nfs_info *nfs_infop;
	struct nfs_client *clp = nfss->nfs_client;
	u32 version = clp->rpc_ops->version;
	int local_flock, local_fcntl;

	nfs_show_nfs_version(m, version, clp->cl_minorversion);
	seq_printf(m, ",rsize=%u", nfss->rsize);
	seq_printf(m, ",wsize=%u", nfss->wsize);
	if (nfss->bsize != 0)
		seq_printf(m, ",bsize=%u", nfss->bsize);
	seq_printf(m, ",namlen=%u", nfss->namelen);
	if (nfss->acregmin != NFS_DEF_ACREGMIN*HZ || showdefaults)
		seq_printf(m, ",acregmin=%u", nfss->acregmin/HZ);
	if (nfss->acregmax != NFS_DEF_ACREGMAX*HZ || showdefaults)
		seq_printf(m, ",acregmax=%u", nfss->acregmax/HZ);
	if (nfss->acdirmin != NFS_DEF_ACDIRMIN*HZ || showdefaults)
		seq_printf(m, ",acdirmin=%u", nfss->acdirmin/HZ);
	if (nfss->acdirmax != NFS_DEF_ACDIRMAX*HZ || showdefaults)
		seq_printf(m, ",acdirmax=%u", nfss->acdirmax/HZ);
	for (nfs_infop = nfs_info; nfs_infop->flag; nfs_infop++) {
		if (nfss->flags & nfs_infop->flag)
			seq_puts(m, nfs_infop->str);
		else
			seq_puts(m, nfs_infop->nostr);
	}
	rcu_read_lock();
	seq_printf(m, ",proto=%s",
		   rpc_peeraddr2str(nfss->client, RPC_DISPLAY_NETID));
	rcu_read_unlock();
	if (version == 4) {
		if (nfss->port != NFS_PORT)
			seq_printf(m, ",port=%u", nfss->port);
	} else
		if (nfss->port)
			seq_printf(m, ",port=%u", nfss->port);

	seq_printf(m, ",timeo=%lu", 10U * nfss->client->cl_timeout->to_initval / HZ);
	seq_printf(m, ",retrans=%u", nfss->client->cl_timeout->to_retries);
	seq_printf(m, ",sec=%s", nfs_pseudoflavour_to_name(nfss->client->cl_auth->au_flavor));

	if (version != 4)
		nfs_show_mountd_options(m, nfss, showdefaults);
	else
		nfs_show_nfsv4_options(m, nfss, showdefaults);

	if (nfss->options & NFS_OPTION_FSCACHE)
		seq_printf(m, ",fsc");

	if (nfss->options & NFS_OPTION_MIGRATION)
		seq_printf(m, ",migration");

	if (nfss->flags & NFS_MOUNT_LOOKUP_CACHE_NONEG) {
		if (nfss->flags & NFS_MOUNT_LOOKUP_CACHE_NONE)
			seq_printf(m, ",lookupcache=none");
		else
			seq_printf(m, ",lookupcache=pos");
	}

	local_flock = nfss->flags & NFS_MOUNT_LOCAL_FLOCK;
	local_fcntl = nfss->flags & NFS_MOUNT_LOCAL_FCNTL;

	if (!local_flock && !local_fcntl)
		seq_printf(m, ",local_lock=none");
	else if (local_flock && local_fcntl)
		seq_printf(m, ",local_lock=all");
	else if (local_flock)
		seq_printf(m, ",local_lock=flock");
	else
		seq_printf(m, ",local_lock=posix");
}

/*
 * Describe the mount options on this VFS mountpoint
 */
int nfs_show_options(struct seq_file *m, struct dentry *root)
{
	struct nfs_server *nfss = NFS_SB(root->d_sb);

	nfs_show_mount_options(m, nfss, 0);

	rcu_read_lock();
	seq_printf(m, ",addr=%s",
			rpc_peeraddr2str(nfss->nfs_client->cl_rpcclient,
							RPC_DISPLAY_ADDR));
	rcu_read_unlock();

	return 0;
}
EXPORT_SYMBOL_GPL(nfs_show_options);

#if IS_ENABLED(CONFIG_NFS_V4)
#ifdef CONFIG_NFS_V4_1
static void show_sessions(struct seq_file *m, struct nfs_server *server)
{
	if (nfs4_has_session(server->nfs_client))
		seq_printf(m, ",sessions");
}
#else
static void show_sessions(struct seq_file *m, struct nfs_server *server) {}
#endif
#endif

#ifdef CONFIG_NFS_V4_1
static void show_pnfs(struct seq_file *m, struct nfs_server *server)
{
	seq_printf(m, ",pnfs=");
	if (server->pnfs_curr_ld)
		seq_printf(m, "%s", server->pnfs_curr_ld->name);
	else
		seq_printf(m, "not configured");
}

static void show_implementation_id(struct seq_file *m, struct nfs_server *nfss)
{
	if (nfss->nfs_client && nfss->nfs_client->cl_implid) {
		struct nfs41_impl_id *impl_id = nfss->nfs_client->cl_implid;
		seq_printf(m, "\n\timpl_id:\tname='%s',domain='%s',"
			   "date='%llu,%u'",
			   impl_id->name, impl_id->domain,
			   impl_id->date.seconds, impl_id->date.nseconds);
	}
}
#else
#if IS_ENABLED(CONFIG_NFS_V4)
static void show_pnfs(struct seq_file *m, struct nfs_server *server)
{
}
#endif
static void show_implementation_id(struct seq_file *m, struct nfs_server *nfss)
{
}
#endif

int nfs_show_devname(struct seq_file *m, struct dentry *root)
{
	char *page = (char *) __get_free_page(GFP_KERNEL);
	char *devname, *dummy;
	int err = 0;
	if (!page)
		return -ENOMEM;
	devname = nfs_path(&dummy, root, page, PAGE_SIZE, 0);
	if (IS_ERR(devname))
		err = PTR_ERR(devname);
	else
		seq_escape(m, devname, " \t\n\\");
	free_page((unsigned long)page);
	return err;
}
EXPORT_SYMBOL_GPL(nfs_show_devname);

int nfs_show_path(struct seq_file *m, struct dentry *dentry)
{
	seq_puts(m, "/");
	return 0;
}
EXPORT_SYMBOL_GPL(nfs_show_path);

/*
 * Present statistical information for this VFS mountpoint
 */
int nfs_show_stats(struct seq_file *m, struct dentry *root)
{
	int i, cpu;
	struct nfs_server *nfss = NFS_SB(root->d_sb);
	struct rpc_auth *auth = nfss->client->cl_auth;
	struct nfs_iostats totals = { };

	seq_printf(m, "statvers=%s", NFS_IOSTAT_VERS);

	/*
	 * Display all mount option settings
	 */
	seq_printf(m, "\n\topts:\t");
	seq_puts(m, sb_rdonly(root->d_sb) ? "ro" : "rw");
	seq_puts(m, root->d_sb->s_flags & SB_SYNCHRONOUS ? ",sync" : "");
	seq_puts(m, root->d_sb->s_flags & SB_NOATIME ? ",noatime" : "");
	seq_puts(m, root->d_sb->s_flags & SB_NODIRATIME ? ",nodiratime" : "");
	nfs_show_mount_options(m, nfss, 1);

	seq_printf(m, "\n\tage:\t%lu", (jiffies - nfss->mount_time) / HZ);

	show_implementation_id(m, nfss);

	seq_printf(m, "\n\tcaps:\t");
	seq_printf(m, "caps=0x%x", nfss->caps);
	seq_printf(m, ",wtmult=%u", nfss->wtmult);
	seq_printf(m, ",dtsize=%u", nfss->dtsize);
	seq_printf(m, ",bsize=%u", nfss->bsize);
	seq_printf(m, ",namlen=%u", nfss->namelen);

#if IS_ENABLED(CONFIG_NFS_V4)
	if (nfss->nfs_client->rpc_ops->version == 4) {
		seq_printf(m, "\n\tnfsv4:\t");
		seq_printf(m, "bm0=0x%x", nfss->attr_bitmask[0]);
		seq_printf(m, ",bm1=0x%x", nfss->attr_bitmask[1]);
		seq_printf(m, ",bm2=0x%x", nfss->attr_bitmask[2]);
		seq_printf(m, ",acl=0x%x", nfss->acl_bitmask);
		show_sessions(m, nfss);
		show_pnfs(m, nfss);
	}
#endif

	/*
	 * Display security flavor in effect for this mount
	 */
	seq_printf(m, "\n\tsec:\tflavor=%u", auth->au_ops->au_flavor);
	if (auth->au_flavor)
		seq_printf(m, ",pseudoflavor=%u", auth->au_flavor);

	/*
	 * Display superblock I/O counters
	 */
	for_each_possible_cpu(cpu) {
		struct nfs_iostats *stats;

		preempt_disable();
		stats = per_cpu_ptr(nfss->io_stats, cpu);

		for (i = 0; i < __NFSIOS_COUNTSMAX; i++)
			totals.events[i] += stats->events[i];
		for (i = 0; i < __NFSIOS_BYTESMAX; i++)
			totals.bytes[i] += stats->bytes[i];
#ifdef CONFIG_NFS_FSCACHE
		for (i = 0; i < __NFSIOS_FSCACHEMAX; i++)
			totals.fscache[i] += stats->fscache[i];
#endif

		preempt_enable();
	}

	seq_printf(m, "\n\tevents:\t");
	for (i = 0; i < __NFSIOS_COUNTSMAX; i++)
		seq_printf(m, "%lu ", totals.events[i]);
	seq_printf(m, "\n\tbytes:\t");
	for (i = 0; i < __NFSIOS_BYTESMAX; i++)
		seq_printf(m, "%Lu ", totals.bytes[i]);
#ifdef CONFIG_NFS_FSCACHE
	if (nfss->options & NFS_OPTION_FSCACHE) {
		seq_printf(m, "\n\tfsc:\t");
		for (i = 0; i < __NFSIOS_FSCACHEMAX; i++)
			seq_printf(m, "%Lu ", totals.fscache[i]);
	}
#endif
	seq_printf(m, "\n");

	rpc_clnt_show_stats(m, nfss->client);

	return 0;
}
EXPORT_SYMBOL_GPL(nfs_show_stats);

/*
 * Begin unmount by attempting to remove all automounted mountpoints we added
 * in response to xdev traversals and referrals
 */
void nfs_umount_begin(struct super_block *sb)
{
	struct nfs_server *server;
	struct rpc_clnt *rpc;

	server = NFS_SB(sb);
	/* -EIO all pending I/O */
	rpc = server->client_acl;
	if (!IS_ERR(rpc))
		rpc_killall_tasks(rpc);
	rpc = server->client;
	if (!IS_ERR(rpc))
		rpc_killall_tasks(rpc);
}
EXPORT_SYMBOL_GPL(nfs_umount_begin);

static struct nfs_parsed_mount_data *nfs_alloc_parsed_mount_data(void)
{
	struct nfs_parsed_mount_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data) {
		data->timeo		= NFS_UNSPEC_TIMEO;
		data->retrans		= NFS_UNSPEC_RETRANS;
		data->acregmin		= NFS_DEF_ACREGMIN;
		data->acregmax		= NFS_DEF_ACREGMAX;
		data->acdirmin		= NFS_DEF_ACDIRMIN;
		data->acdirmax		= NFS_DEF_ACDIRMAX;
		data->mount_server.port	= NFS_UNSPEC_PORT;
		data->nfs_server.port	= NFS_UNSPEC_PORT;
		data->nfs_server.protocol = XPRT_TRANSPORT_TCP;
		data->selected_flavor	= RPC_AUTH_MAXFLAVOR;
		data->minorversion	= 0;
		data->need_mount	= true;
		data->net		= current->nsproxy->net_ns;
		data->lsm_opts		= NULL;
	}
	return data;
}

static void nfs_free_parsed_mount_data(struct nfs_parsed_mount_data *data)
{
	if (data) {
		kfree(data->client_address);
		kfree(data->mount_server.hostname);
		kfree(data->nfs_server.export_path);
		kfree(data->nfs_server.hostname);
		kfree(data->fscache_uniq);
		security_free_mnt_opts(&data->lsm_opts);
		kfree(data);
	}
}

/*
 * Sanity-check a server address provided by the mount command.
 *
 * Address family must be initialized, and address must not be
 * the ANY address for that family.
 */
static int nfs_verify_server_address(struct sockaddr *addr)
{
	switch (addr->sa_family) {
	case AF_INET: {
		struct sockaddr_in *sa = (struct sockaddr_in *)addr;
		return sa->sin_addr.s_addr != htonl(INADDR_ANY);
	}
	case AF_INET6: {
		struct in6_addr *sa = &((struct sockaddr_in6 *)addr)->sin6_addr;
		return !ipv6_addr_any(sa);
	}
	}

	dfprintk(MOUNT, "NFS: Invalid IP address specified\n");
	return 0;
}

/*
 * Select between a default port value and a user-specified port value.
 * If a zero value is set, then autobind will be used.
 */
static void nfs_set_port(struct sockaddr *sap, int *port,
				 const unsigned short default_port)
{
	if (*port == NFS_UNSPEC_PORT)
		*port = default_port;

	rpc_set_port(sap, *port);
}

/*
 * Sanity check the NFS transport protocol.
 *
 */
static void nfs_validate_transport_protocol(struct nfs_parsed_mount_data *mnt)
{
	switch (mnt->nfs_server.protocol) {
	case XPRT_TRANSPORT_UDP:
	case XPRT_TRANSPORT_TCP:
	case XPRT_TRANSPORT_RDMA:
		break;
	default:
		mnt->nfs_server.protocol = XPRT_TRANSPORT_TCP;
	}
}

/*
 * For text based NFSv2/v3 mounts, the mount protocol transport default
 * settings should depend upon the specified NFS transport.
 */
static void nfs_set_mount_transport_protocol(struct nfs_parsed_mount_data *mnt)
{
	nfs_validate_transport_protocol(mnt);

	if (mnt->mount_server.protocol == XPRT_TRANSPORT_UDP ||
	    mnt->mount_server.protocol == XPRT_TRANSPORT_TCP)
			return;
	switch (mnt->nfs_server.protocol) {
	case XPRT_TRANSPORT_UDP:
		mnt->mount_server.protocol = XPRT_TRANSPORT_UDP;
		break;
	case XPRT_TRANSPORT_TCP:
	case XPRT_TRANSPORT_RDMA:
		mnt->mount_server.protocol = XPRT_TRANSPORT_TCP;
	}
}

/*
 * Add 'flavor' to 'auth_info' if not already present.
 * Returns true if 'flavor' ends up in the list, false otherwise
 */
static bool nfs_auth_info_add(struct nfs_auth_info *auth_info,
			      rpc_authflavor_t flavor)
{
	unsigned int i;
	unsigned int max_flavor_len = ARRAY_SIZE(auth_info->flavors);

	/* make sure this flavor isn't already in the list */
	for (i = 0; i < auth_info->flavor_len; i++) {
		if (flavor == auth_info->flavors[i])
			return true;
	}

	if (auth_info->flavor_len + 1 >= max_flavor_len) {
		dfprintk(MOUNT, "NFS: too many sec= flavors\n");
		return false;
	}

	auth_info->flavors[auth_info->flavor_len++] = flavor;
	return true;
}

/*
 * Return true if 'match' is in auth_info or auth_info is empty.
 * Return false otherwise.
 */
bool nfs_auth_info_match(const struct nfs_auth_info *auth_info,
			 rpc_authflavor_t match)
{
	int i;

	if (!auth_info->flavor_len)
		return true;

	for (i = 0; i < auth_info->flavor_len; i++) {
		if (auth_info->flavors[i] == match)
			return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(nfs_auth_info_match);

/*
 * Parse the value of the 'sec=' option.
 */
static int nfs_parse_security_flavors(char *value,
				      struct nfs_parsed_mount_data *mnt)
{
	substring_t args[MAX_OPT_ARGS];
	rpc_authflavor_t pseudoflavor;
	char *p;

	dfprintk(MOUNT, "NFS: parsing sec=%s option\n", value);

	while ((p = strsep(&value, ":")) != NULL) {
		switch (match_token(p, nfs_secflavor_tokens, args)) {
		case Opt_sec_none:
			pseudoflavor = RPC_AUTH_NULL;
			break;
		case Opt_sec_sys:
			pseudoflavor = RPC_AUTH_UNIX;
			break;
		case Opt_sec_krb5:
			pseudoflavor = RPC_AUTH_GSS_KRB5;
			break;
		case Opt_sec_krb5i:
			pseudoflavor = RPC_AUTH_GSS_KRB5I;
			break;
		case Opt_sec_krb5p:
			pseudoflavor = RPC_AUTH_GSS_KRB5P;
			break;
		case Opt_sec_lkey:
			pseudoflavor = RPC_AUTH_GSS_LKEY;
			break;
		case Opt_sec_lkeyi:
			pseudoflavor = RPC_AUTH_GSS_LKEYI;
			break;
		case Opt_sec_lkeyp:
			pseudoflavor = RPC_AUTH_GSS_LKEYP;
			break;
		case Opt_sec_spkm:
			pseudoflavor = RPC_AUTH_GSS_SPKM;
			break;
		case Opt_sec_spkmi:
			pseudoflavor = RPC_AUTH_GSS_SPKMI;
			break;
		case Opt_sec_spkmp:
			pseudoflavor = RPC_AUTH_GSS_SPKMP;
			break;
		default:
			dfprintk(MOUNT,
				 "NFS: sec= option '%s' not recognized\n", p);
			return 0;
		}

		if (!nfs_auth_info_add(&mnt->auth_info, pseudoflavor))
			return 0;
	}

	return 1;
}

static int nfs_parse_version_string(char *string,
		struct nfs_parsed_mount_data *mnt,
		substring_t *args)
{
	mnt->flags &= ~NFS_MOUNT_VER3;
	switch (match_token(string, nfs_vers_tokens, args)) {
	case Opt_vers_2:
		mnt->version = 2;
		break;
	case Opt_vers_3:
		mnt->flags |= NFS_MOUNT_VER3;
		mnt->version = 3;
		break;
	case Opt_vers_4:
		/* Backward compatibility option. In future,
		 * the mount program should always supply
		 * a NFSv4 minor version number.
		 */
		mnt->version = 4;
		break;
	case Opt_vers_4_0:
		mnt->version = 4;
		mnt->minorversion = 0;
		break;
	case Opt_vers_4_1:
		mnt->version = 4;
		mnt->minorversion = 1;
		break;
	case Opt_vers_4_2:
		mnt->version = 4;
		mnt->minorversion = 2;
		break;
	default:
		return 0;
	}
	return 1;
}

static int nfs_get_option_str(substring_t args[], char **option)
{
	kfree(*option);
	*option = match_strdup(args);
	return !*option;
}

static int nfs_get_option_ul(substring_t args[], unsigned long *option)
{
	int rc;
	char *string;

	string = match_strdup(args);
	if (string == NULL)
		return -ENOMEM;
	rc = kstrtoul(string, 10, option);
	kfree(string);

	return rc;
}

static int nfs_get_option_ul_bound(substring_t args[], unsigned long *option,
		unsigned long l_bound, unsigned long u_bound)
{
	int ret;

	ret = nfs_get_option_ul(args, option);
	if (ret != 0)
		return ret;
	if (*option < l_bound || *option > u_bound)
		return -ERANGE;
	return 0;
}

/*
 * Error-check and convert a string of mount options from user space into
 * a data structure.  The whole mount string is processed; bad options are
 * skipped as they are encountered.  If there were no errors, return 1;
 * otherwise return 0 (zero).
 */
static int nfs_parse_mount_options(char *raw,
				   struct nfs_parsed_mount_data *mnt)
{
	char *p, *string;
	int rc, sloppy = 0, invalid_option = 0;
	unsigned short protofamily = AF_UNSPEC;
	unsigned short mountfamily = AF_UNSPEC;

	if (!raw) {
		dfprintk(MOUNT, "NFS: mount options string was NULL.\n");
		return 1;
	}
	dfprintk(MOUNT, "NFS: nfs mount opts='%s'\n", raw);

	rc = security_sb_eat_lsm_opts(raw, &mnt->lsm_opts);
	if (rc)
		goto out_security_failure;

	while ((p = strsep(&raw, ",")) != NULL) {
		substring_t args[MAX_OPT_ARGS];
		unsigned long option;
		int token;

		if (!*p)
			continue;

		dfprintk(MOUNT, "NFS:   parsing nfs mount option '%s'\n", p);

		token = match_token(p, nfs_mount_option_tokens, args);
		switch (token) {

		/*
		 * boolean options:  foo/nofoo
		 */
		case Opt_soft:
			mnt->flags |= NFS_MOUNT_SOFT;
			break;
		case Opt_hard:
			mnt->flags &= ~NFS_MOUNT_SOFT;
			break;
		case Opt_posix:
			mnt->flags |= NFS_MOUNT_POSIX;
			break;
		case Opt_noposix:
			mnt->flags &= ~NFS_MOUNT_POSIX;
			break;
		case Opt_cto:
			mnt->flags &= ~NFS_MOUNT_NOCTO;
			break;
		case Opt_nocto:
			mnt->flags |= NFS_MOUNT_NOCTO;
			break;
		case Opt_ac:
			mnt->flags &= ~NFS_MOUNT_NOAC;
			break;
		case Opt_noac:
			mnt->flags |= NFS_MOUNT_NOAC;
			break;
		case Opt_lock:
			mnt->flags &= ~NFS_MOUNT_NONLM;
			mnt->flags &= ~(NFS_MOUNT_LOCAL_FLOCK |
					NFS_MOUNT_LOCAL_FCNTL);
			break;
		case Opt_nolock:
			mnt->flags |= NFS_MOUNT_NONLM;
			mnt->flags |= (NFS_MOUNT_LOCAL_FLOCK |
				       NFS_MOUNT_LOCAL_FCNTL);
			break;
		case Opt_udp:
			mnt->flags &= ~NFS_MOUNT_TCP;
			mnt->nfs_server.protocol = XPRT_TRANSPORT_UDP;
			break;
		case Opt_tcp:
			mnt->flags |= NFS_MOUNT_TCP;
			mnt->nfs_server.protocol = XPRT_TRANSPORT_TCP;
			break;
		case Opt_rdma:
			mnt->flags |= NFS_MOUNT_TCP; /* for side protocols */
			mnt->nfs_server.protocol = XPRT_TRANSPORT_RDMA;
			xprt_load_transport(p);
			break;
		case Opt_acl:
			mnt->flags &= ~NFS_MOUNT_NOACL;
			break;
		case Opt_noacl:
			mnt->flags |= NFS_MOUNT_NOACL;
			break;
		case Opt_rdirplus:
			mnt->flags &= ~NFS_MOUNT_NORDIRPLUS;
			break;
		case Opt_nordirplus:
			mnt->flags |= NFS_MOUNT_NORDIRPLUS;
			break;
		case Opt_sharecache:
			mnt->flags &= ~NFS_MOUNT_UNSHARED;
			break;
		case Opt_nosharecache:
			mnt->flags |= NFS_MOUNT_UNSHARED;
			break;
		case Opt_resvport:
			mnt->flags &= ~NFS_MOUNT_NORESVPORT;
			break;
		case Opt_noresvport:
			mnt->flags |= NFS_MOUNT_NORESVPORT;
			break;
		case Opt_fscache:
			mnt->options |= NFS_OPTION_FSCACHE;
			kfree(mnt->fscache_uniq);
			mnt->fscache_uniq = NULL;
			break;
		case Opt_nofscache:
			mnt->options &= ~NFS_OPTION_FSCACHE;
			kfree(mnt->fscache_uniq);
			mnt->fscache_uniq = NULL;
			break;
		case Opt_migration:
			mnt->options |= NFS_OPTION_MIGRATION;
			break;
		case Opt_nomigration:
			mnt->options &= ~NFS_OPTION_MIGRATION;
			break;

		/*
		 * options that take numeric values
		 */
		case Opt_port:
			if (nfs_get_option_ul(args, &option) ||
			    option > USHRT_MAX)
				goto out_invalid_value;
			mnt->nfs_server.port = option;
			break;
		case Opt_rsize:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			mnt->rsize = option;
			break;
		case Opt_wsize:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			mnt->wsize = option;
			break;
		case Opt_bsize:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			mnt->bsize = option;
			break;
		case Opt_timeo:
			if (nfs_get_option_ul_bound(args, &option, 1, INT_MAX))
				goto out_invalid_value;
			mnt->timeo = option;
			break;
		case Opt_retrans:
			if (nfs_get_option_ul_bound(args, &option, 0, INT_MAX))
				goto out_invalid_value;
			mnt->retrans = option;
			break;
		case Opt_acregmin:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			mnt->acregmin = option;
			break;
		case Opt_acregmax:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			mnt->acregmax = option;
			break;
		case Opt_acdirmin:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			mnt->acdirmin = option;
			break;
		case Opt_acdirmax:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			mnt->acdirmax = option;
			break;
		case Opt_actimeo:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			mnt->acregmin = mnt->acregmax =
			mnt->acdirmin = mnt->acdirmax = option;
			break;
		case Opt_namelen:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			mnt->namlen = option;
			break;
		case Opt_mountport:
			if (nfs_get_option_ul(args, &option) ||
			    option > USHRT_MAX)
				goto out_invalid_value;
			mnt->mount_server.port = option;
			break;
		case Opt_mountvers:
			if (nfs_get_option_ul(args, &option) ||
			    option < NFS_MNT_VERSION ||
			    option > NFS_MNT3_VERSION)
				goto out_invalid_value;
			mnt->mount_server.version = option;
			break;
		case Opt_minorversion:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			if (option > NFS4_MAX_MINOR_VERSION)
				goto out_invalid_value;
			mnt->minorversion = option;
			break;

		/*
		 * options that take text values
		 */
		case Opt_nfsvers:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			rc = nfs_parse_version_string(string, mnt, args);
			kfree(string);
			if (!rc)
				goto out_invalid_value;
			break;
		case Opt_sec:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			rc = nfs_parse_security_flavors(string, mnt);
			kfree(string);
			if (!rc) {
				dfprintk(MOUNT, "NFS:   unrecognized "
						"security flavor\n");
				return 0;
			}
			break;
		case Opt_proto:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			token = match_token(string,
					    nfs_xprt_protocol_tokens, args);

			protofamily = AF_INET;
			switch (token) {
			case Opt_xprt_udp6:
				protofamily = AF_INET6;
				/* fall through */
			case Opt_xprt_udp:
				mnt->flags &= ~NFS_MOUNT_TCP;
				mnt->nfs_server.protocol = XPRT_TRANSPORT_UDP;
				break;
			case Opt_xprt_tcp6:
				protofamily = AF_INET6;
				/* fall through */
			case Opt_xprt_tcp:
				mnt->flags |= NFS_MOUNT_TCP;
				mnt->nfs_server.protocol = XPRT_TRANSPORT_TCP;
				break;
			case Opt_xprt_rdma6:
				protofamily = AF_INET6;
				/* fall through */
			case Opt_xprt_rdma:
				/* vector side protocols to TCP */
				mnt->flags |= NFS_MOUNT_TCP;
				mnt->nfs_server.protocol = XPRT_TRANSPORT_RDMA;
				xprt_load_transport(string);
				break;
			default:
				dfprintk(MOUNT, "NFS:   unrecognized "
						"transport protocol\n");
				kfree(string);
				return 0;
			}
			kfree(string);
			break;
		case Opt_mountproto:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			token = match_token(string,
					    nfs_xprt_protocol_tokens, args);
			kfree(string);

			mountfamily = AF_INET;
			switch (token) {
			case Opt_xprt_udp6:
				mountfamily = AF_INET6;
				/* fall through */
			case Opt_xprt_udp:
				mnt->mount_server.protocol = XPRT_TRANSPORT_UDP;
				break;
			case Opt_xprt_tcp6:
				mountfamily = AF_INET6;
				/* fall through */
			case Opt_xprt_tcp:
				mnt->mount_server.protocol = XPRT_TRANSPORT_TCP;
				break;
			case Opt_xprt_rdma: /* not used for side protocols */
			default:
				dfprintk(MOUNT, "NFS:   unrecognized "
						"transport protocol\n");
				return 0;
			}
			break;
		case Opt_addr:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			mnt->nfs_server.addrlen =
				rpc_pton(mnt->net, string, strlen(string),
					(struct sockaddr *)
					&mnt->nfs_server.address,
					sizeof(mnt->nfs_server.address));
			kfree(string);
			if (mnt->nfs_server.addrlen == 0)
				goto out_invalid_address;
			break;
		case Opt_clientaddr:
			if (nfs_get_option_str(args, &mnt->client_address))
				goto out_nomem;
			break;
		case Opt_mounthost:
			if (nfs_get_option_str(args,
					       &mnt->mount_server.hostname))
				goto out_nomem;
			break;
		case Opt_mountaddr:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			mnt->mount_server.addrlen =
				rpc_pton(mnt->net, string, strlen(string),
					(struct sockaddr *)
					&mnt->mount_server.address,
					sizeof(mnt->mount_server.address));
			kfree(string);
			if (mnt->mount_server.addrlen == 0)
				goto out_invalid_address;
			break;
		case Opt_lookupcache:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			token = match_token(string,
					nfs_lookupcache_tokens, args);
			kfree(string);
			switch (token) {
				case Opt_lookupcache_all:
					mnt->flags &= ~(NFS_MOUNT_LOOKUP_CACHE_NONEG|NFS_MOUNT_LOOKUP_CACHE_NONE);
					break;
				case Opt_lookupcache_positive:
					mnt->flags &= ~NFS_MOUNT_LOOKUP_CACHE_NONE;
					mnt->flags |= NFS_MOUNT_LOOKUP_CACHE_NONEG;
					break;
				case Opt_lookupcache_none:
					mnt->flags |= NFS_MOUNT_LOOKUP_CACHE_NONEG|NFS_MOUNT_LOOKUP_CACHE_NONE;
					break;
				default:
					dfprintk(MOUNT, "NFS:   invalid "
							"lookupcache argument\n");
					return 0;
			};
			break;
		case Opt_fscache_uniq:
			if (nfs_get_option_str(args, &mnt->fscache_uniq))
				goto out_nomem;
			mnt->options |= NFS_OPTION_FSCACHE;
			break;
		case Opt_local_lock:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			token = match_token(string, nfs_local_lock_tokens,
					args);
			kfree(string);
			switch (token) {
			case Opt_local_lock_all:
				mnt->flags |= (NFS_MOUNT_LOCAL_FLOCK |
					       NFS_MOUNT_LOCAL_FCNTL);
				break;
			case Opt_local_lock_flock:
				mnt->flags |= NFS_MOUNT_LOCAL_FLOCK;
				break;
			case Opt_local_lock_posix:
				mnt->flags |= NFS_MOUNT_LOCAL_FCNTL;
				break;
			case Opt_local_lock_none:
				mnt->flags &= ~(NFS_MOUNT_LOCAL_FLOCK |
						NFS_MOUNT_LOCAL_FCNTL);
				break;
			default:
				dfprintk(MOUNT, "NFS:	invalid	"
						"local_lock argument\n");
				return 0;
			};
			break;

		/*
		 * Special options
		 */
		case Opt_sloppy:
			sloppy = 1;
			dfprintk(MOUNT, "NFS:   relaxing parsing rules\n");
			break;
		case Opt_userspace:
		case Opt_deprecated:
			dfprintk(MOUNT, "NFS:   ignoring mount option "
					"'%s'\n", p);
			break;

		default:
			invalid_option = 1;
			dfprintk(MOUNT, "NFS:   unrecognized mount option "
					"'%s'\n", p);
		}
	}

	if (!sloppy && invalid_option)
		return 0;

	if (mnt->minorversion && mnt->version != 4)
		goto out_minorversion_mismatch;

	if (mnt->options & NFS_OPTION_MIGRATION &&
	    (mnt->version != 4 || mnt->minorversion != 0))
		goto out_migration_misuse;

	/*
	 * verify that any proto=/mountproto= options match the address
	 * families in the addr=/mountaddr= options.
	 */
	if (protofamily != AF_UNSPEC &&
	    protofamily != mnt->nfs_server.address.ss_family)
		goto out_proto_mismatch;

	if (mountfamily != AF_UNSPEC) {
		if (mnt->mount_server.addrlen) {
			if (mountfamily != mnt->mount_server.address.ss_family)
				goto out_mountproto_mismatch;
		} else {
			if (mountfamily != mnt->nfs_server.address.ss_family)
				goto out_mountproto_mismatch;
		}
	}

	return 1;

out_mountproto_mismatch:
	printk(KERN_INFO "NFS: mount server address does not match mountproto= "
			 "option\n");
	return 0;
out_proto_mismatch:
	printk(KERN_INFO "NFS: server address does not match proto= option\n");
	return 0;
out_invalid_address:
	printk(KERN_INFO "NFS: bad IP address specified: %s\n", p);
	return 0;
out_invalid_value:
	printk(KERN_INFO "NFS: bad mount option value specified: %s\n", p);
	return 0;
out_minorversion_mismatch:
	printk(KERN_INFO "NFS: mount option vers=%u does not support "
			 "minorversion=%u\n", mnt->version, mnt->minorversion);
	return 0;
out_migration_misuse:
	printk(KERN_INFO
		"NFS: 'migration' not supported for this NFS version\n");
	return 0;
out_nomem:
	printk(KERN_INFO "NFS: not enough memory to parse option\n");
	return 0;
out_security_failure:
	printk(KERN_INFO "NFS: security options invalid: %d\n", rc);
	return 0;
}

/*
 * Ensure that a specified authtype in args->auth_info is supported by
 * the server. Returns 0 and sets args->selected_flavor if it's ok, and
 * -EACCES if not.
 */
static int nfs_verify_authflavors(struct nfs_parsed_mount_data *args,
			rpc_authflavor_t *server_authlist, unsigned int count)
{
	rpc_authflavor_t flavor = RPC_AUTH_MAXFLAVOR;
	bool found_auth_null = false;
	unsigned int i;

	/*
	 * If the sec= mount option is used, the specified flavor or AUTH_NULL
	 * must be in the list returned by the server.
	 *
	 * AUTH_NULL has a special meaning when it's in the server list - it
	 * means that the server will ignore the rpc creds, so any flavor
	 * can be used but still use the sec= that was specified.
	 *
	 * Note also that the MNT procedure in MNTv1 does not return a list
	 * of supported security flavors. In this case, nfs_mount() fabricates
	 * a security flavor list containing just AUTH_NULL.
	 */
	for (i = 0; i < count; i++) {
		flavor = server_authlist[i];

		if (nfs_auth_info_match(&args->auth_info, flavor))
			goto out;

		if (flavor == RPC_AUTH_NULL)
			found_auth_null = true;
	}

	if (found_auth_null) {
		flavor = args->auth_info.flavors[0];
		goto out;
	}

	dfprintk(MOUNT,
		 "NFS: specified auth flavors not supported by server\n");
	return -EACCES;

out:
	args->selected_flavor = flavor;
	dfprintk(MOUNT, "NFS: using auth flavor %u\n", args->selected_flavor);
	return 0;
}

/*
 * Use the remote server's MOUNT service to request the NFS file handle
 * corresponding to the provided path.
 */
static int nfs_request_mount(struct nfs_parsed_mount_data *args,
			     struct nfs_fh *root_fh,
			     rpc_authflavor_t *server_authlist,
			     unsigned int *server_authlist_len)
{
	struct nfs_mount_request request = {
		.sap		= (struct sockaddr *)
						&args->mount_server.address,
		.dirpath	= args->nfs_server.export_path,
		.protocol	= args->mount_server.protocol,
		.fh		= root_fh,
		.noresvport	= args->flags & NFS_MOUNT_NORESVPORT,
		.auth_flav_len	= server_authlist_len,
		.auth_flavs	= server_authlist,
		.net		= args->net,
	};
	int status;

	if (args->mount_server.version == 0) {
		switch (args->version) {
			default:
				args->mount_server.version = NFS_MNT3_VERSION;
				break;
			case 2:
				args->mount_server.version = NFS_MNT_VERSION;
		}
	}
	request.version = args->mount_server.version;

	if (args->mount_server.hostname)
		request.hostname = args->mount_server.hostname;
	else
		request.hostname = args->nfs_server.hostname;

	/*
	 * Construct the mount server's address.
	 */
	if (args->mount_server.address.ss_family == AF_UNSPEC) {
		memcpy(request.sap, &args->nfs_server.address,
		       args->nfs_server.addrlen);
		args->mount_server.addrlen = args->nfs_server.addrlen;
	}
	request.salen = args->mount_server.addrlen;
	nfs_set_port(request.sap, &args->mount_server.port, 0);

	/*
	 * Now ask the mount server to map our export path
	 * to a file handle.
	 */
	status = nfs_mount(&request);
	if (status != 0) {
		dfprintk(MOUNT, "NFS: unable to mount server %s, error %d\n",
				request.hostname, status);
		return status;
	}

	return 0;
}

static struct nfs_server *nfs_try_mount_request(struct nfs_mount_info *mount_info,
					struct nfs_subversion *nfs_mod)
{
	int status;
	unsigned int i;
	bool tried_auth_unix = false;
	bool auth_null_in_list = false;
	struct nfs_server *server = ERR_PTR(-EACCES);
	struct nfs_parsed_mount_data *args = mount_info->parsed;
	rpc_authflavor_t authlist[NFS_MAX_SECFLAVORS];
	unsigned int authlist_len = ARRAY_SIZE(authlist);

	status = nfs_request_mount(args, mount_info->mntfh, authlist,
					&authlist_len);
	if (status)
		return ERR_PTR(status);

	/*
	 * Was a sec= authflavor specified in the options? First, verify
	 * whether the server supports it, and then just try to use it if so.
	 */
	if (args->auth_info.flavor_len > 0) {
		status = nfs_verify_authflavors(args, authlist, authlist_len);
		dfprintk(MOUNT, "NFS: using auth flavor %u\n",
			 args->selected_flavor);
		if (status)
			return ERR_PTR(status);
		return nfs_mod->rpc_ops->create_server(mount_info, nfs_mod);
	}

	/*
	 * No sec= option was provided. RFC 2623, section 2.7 suggests we
	 * SHOULD prefer the flavor listed first. However, some servers list
	 * AUTH_NULL first. Avoid ever choosing AUTH_NULL.
	 */
	for (i = 0; i < authlist_len; ++i) {
		rpc_authflavor_t flavor;
		struct rpcsec_gss_info info;

		flavor = authlist[i];
		switch (flavor) {
		case RPC_AUTH_UNIX:
			tried_auth_unix = true;
			break;
		case RPC_AUTH_NULL:
			auth_null_in_list = true;
			continue;
		default:
			if (rpcauth_get_gssinfo(flavor, &info) != 0)
				continue;
			/* Fallthrough */
		}
		dfprintk(MOUNT, "NFS: attempting to use auth flavor %u\n", flavor);
		args->selected_flavor = flavor;
		server = nfs_mod->rpc_ops->create_server(mount_info, nfs_mod);
		if (!IS_ERR(server))
			return server;
	}

	/*
	 * Nothing we tried so far worked. At this point, give up if we've
	 * already tried AUTH_UNIX or if the server's list doesn't contain
	 * AUTH_NULL
	 */
	if (tried_auth_unix || !auth_null_in_list)
		return server;

	/* Last chance! Try AUTH_UNIX */
	dfprintk(MOUNT, "NFS: attempting to use auth flavor %u\n", RPC_AUTH_UNIX);
	args->selected_flavor = RPC_AUTH_UNIX;
	return nfs_mod->rpc_ops->create_server(mount_info, nfs_mod);
}

struct dentry *nfs_try_mount(int flags, const char *dev_name,
			     struct nfs_mount_info *mount_info,
			     struct nfs_subversion *nfs_mod)
{
	struct nfs_server *server;

	if (mount_info->parsed->need_mount)
		server = nfs_try_mount_request(mount_info, nfs_mod);
	else
		server = nfs_mod->rpc_ops->create_server(mount_info, nfs_mod);

	if (IS_ERR(server))
		return ERR_CAST(server);

	return nfs_fs_mount_common(server, flags, dev_name, mount_info, nfs_mod);
}
EXPORT_SYMBOL_GPL(nfs_try_mount);

/*
 * Split "dev_name" into "hostname:export_path".
 *
 * The leftmost colon demarks the split between the server's hostname
 * and the export path.  If the hostname starts with a left square
 * bracket, then it may contain colons.
 *
 * Note: caller frees hostname and export path, even on error.
 */
static int nfs_parse_devname(const char *dev_name,
			     char **hostname, size_t maxnamlen,
			     char **export_path, size_t maxpathlen)
{
	size_t len;
	char *end;

	if (unlikely(!dev_name || !*dev_name)) {
		dfprintk(MOUNT, "NFS: device name not specified\n");
		return -EINVAL;
	}

	/* Is the host name protected with square brakcets? */
	if (*dev_name == '[') {
		end = strchr(++dev_name, ']');
		if (end == NULL || end[1] != ':')
			goto out_bad_devname;

		len = end - dev_name;
		end++;
	} else {
		char *comma;

		end = strchr(dev_name, ':');
		if (end == NULL)
			goto out_bad_devname;
		len = end - dev_name;

		/* kill possible hostname list: not supported */
		comma = strchr(dev_name, ',');
		if (comma != NULL && comma < end)
			*comma = 0;
	}

	if (len > maxnamlen)
		goto out_hostname;

	/* N.B. caller will free nfs_server.hostname in all cases */
	*hostname = kstrndup(dev_name, len, GFP_KERNEL);
	if (*hostname == NULL)
		goto out_nomem;
	len = strlen(++end);
	if (len > maxpathlen)
		goto out_path;
	*export_path = kstrndup(end, len, GFP_KERNEL);
	if (!*export_path)
		goto out_nomem;

	dfprintk(MOUNT, "NFS: MNTPATH: '%s'\n", *export_path);
	return 0;

out_bad_devname:
	dfprintk(MOUNT, "NFS: device name not in host:path format\n");
	return -EINVAL;

out_nomem:
	dfprintk(MOUNT, "NFS: not enough memory to parse device name\n");
	return -ENOMEM;

out_hostname:
	dfprintk(MOUNT, "NFS: server hostname too long\n");
	return -ENAMETOOLONG;

out_path:
	dfprintk(MOUNT, "NFS: export pathname too long\n");
	return -ENAMETOOLONG;
}

/*
 * Validate the NFS2/NFS3 mount data
 * - fills in the mount root filehandle
 *
 * For option strings, user space handles the following behaviors:
 *
 * + DNS: mapping server host name to IP address ("addr=" option)
 *
 * + failure mode: how to behave if a mount request can't be handled
 *   immediately ("fg/bg" option)
 *
 * + retry: how often to retry a mount request ("retry=" option)
 *
 * + breaking back: trying proto=udp after proto=tcp, v2 after v3,
 *   mountproto=tcp after mountproto=udp, and so on
 */
static int nfs23_validate_mount_data(void *options,
				     struct nfs_parsed_mount_data *args,
				     struct nfs_fh *mntfh,
				     const char *dev_name)
{
	struct nfs_mount_data *data = (struct nfs_mount_data *)options;
	struct sockaddr *sap = (struct sockaddr *)&args->nfs_server.address;
	int extra_flags = NFS_MOUNT_LEGACY_INTERFACE;

	if (data == NULL)
		goto out_no_data;

	args->version = NFS_DEFAULT_VERSION;
	switch (data->version) {
	case 1:
		data->namlen = 0; /* fall through */
	case 2:
		data->bsize = 0; /* fall through */
	case 3:
		if (data->flags & NFS_MOUNT_VER3)
			goto out_no_v3;
		data->root.size = NFS2_FHSIZE;
		memcpy(data->root.data, data->old_root.data, NFS2_FHSIZE);
		/* Turn off security negotiation */
		extra_flags |= NFS_MOUNT_SECFLAVOUR;
		/* fall through */
	case 4:
		if (data->flags & NFS_MOUNT_SECFLAVOUR)
			goto out_no_sec;
		/* fall through */
	case 5:
		memset(data->context, 0, sizeof(data->context));
		/* fall through */
	case 6:
		if (data->flags & NFS_MOUNT_VER3) {
			if (data->root.size > NFS3_FHSIZE || data->root.size == 0)
				goto out_invalid_fh;
			mntfh->size = data->root.size;
			args->version = 3;
		} else {
			mntfh->size = NFS2_FHSIZE;
			args->version = 2;
		}


		memcpy(mntfh->data, data->root.data, mntfh->size);
		if (mntfh->size < sizeof(mntfh->data))
			memset(mntfh->data + mntfh->size, 0,
			       sizeof(mntfh->data) - mntfh->size);

		/*
		 * Translate to nfs_parsed_mount_data, which nfs_fill_super
		 * can deal with.
		 */
		args->flags		= data->flags & NFS_MOUNT_FLAGMASK;
		args->flags		|= extra_flags;
		args->rsize		= data->rsize;
		args->wsize		= data->wsize;
		args->timeo		= data->timeo;
		args->retrans		= data->retrans;
		args->acregmin		= data->acregmin;
		args->acregmax		= data->acregmax;
		args->acdirmin		= data->acdirmin;
		args->acdirmax		= data->acdirmax;
		args->need_mount	= false;

		memcpy(sap, &data->addr, sizeof(data->addr));
		args->nfs_server.addrlen = sizeof(data->addr);
		args->nfs_server.port = ntohs(data->addr.sin_port);
		if (!nfs_verify_server_address(sap))
			goto out_no_address;

		if (!(data->flags & NFS_MOUNT_TCP))
			args->nfs_server.protocol = XPRT_TRANSPORT_UDP;
		/* N.B. caller will free nfs_server.hostname in all cases */
		args->nfs_server.hostname = kstrdup(data->hostname, GFP_KERNEL);
		args->namlen		= data->namlen;
		args->bsize		= data->bsize;

		if (data->flags & NFS_MOUNT_SECFLAVOUR)
			args->selected_flavor = data->pseudoflavor;
		else
			args->selected_flavor = RPC_AUTH_UNIX;
		if (!args->nfs_server.hostname)
			goto out_nomem;

		if (!(data->flags & NFS_MOUNT_NONLM))
			args->flags &= ~(NFS_MOUNT_LOCAL_FLOCK|
					 NFS_MOUNT_LOCAL_FCNTL);
		else
			args->flags |= (NFS_MOUNT_LOCAL_FLOCK|
					NFS_MOUNT_LOCAL_FCNTL);
		/*
		 * The legacy version 6 binary mount data from userspace has a
		 * field used only to transport selinux information into the
		 * the kernel.  To continue to support that functionality we
		 * have a touch of selinux knowledge here in the NFS code. The
		 * userspace code converted context=blah to just blah so we are
		 * converting back to the full string selinux understands.
		 */
		if (data->context[0]){
#ifdef CONFIG_SECURITY_SELINUX
			int rc;
			data->context[NFS_MAX_CONTEXT_LEN] = '\0';
			rc = security_add_mnt_opt("context", data->context,
					strlen(data->context), &args->lsm_opts);
			if (rc)
				return rc;
#else
			return -EINVAL;
#endif
		}

		break;
	default:
		return NFS_TEXT_DATA;
	}

	return 0;

out_no_data:
	dfprintk(MOUNT, "NFS: mount program didn't pass any mount data\n");
	return -EINVAL;

out_no_v3:
	dfprintk(MOUNT, "NFS: nfs_mount_data version %d does not support v3\n",
		 data->version);
	return -EINVAL;

out_no_sec:
	dfprintk(MOUNT, "NFS: nfs_mount_data version supports only AUTH_SYS\n");
	return -EINVAL;

out_nomem:
	dfprintk(MOUNT, "NFS: not enough memory to handle mount options\n");
	return -ENOMEM;

out_no_address:
	dfprintk(MOUNT, "NFS: mount program didn't pass remote address\n");
	return -EINVAL;

out_invalid_fh:
	dfprintk(MOUNT, "NFS: invalid root filehandle\n");
	return -EINVAL;
}

#if IS_ENABLED(CONFIG_NFS_V4)
static int nfs_validate_mount_data(struct file_system_type *fs_type,
				   void *options,
				   struct nfs_parsed_mount_data *args,
				   struct nfs_fh *mntfh,
				   const char *dev_name)
{
	if (fs_type == &nfs_fs_type)
		return nfs23_validate_mount_data(options, args, mntfh, dev_name);
	return nfs4_validate_mount_data(options, args, dev_name);
}
#else
static int nfs_validate_mount_data(struct file_system_type *fs_type,
				   void *options,
				   struct nfs_parsed_mount_data *args,
				   struct nfs_fh *mntfh,
				   const char *dev_name)
{
	return nfs23_validate_mount_data(options, args, mntfh, dev_name);
}
#endif

static int nfs_validate_text_mount_data(void *options,
					struct nfs_parsed_mount_data *args,
					const char *dev_name)
{
	int port = 0;
	int max_namelen = PAGE_SIZE;
	int max_pathlen = NFS_MAXPATHLEN;
	struct sockaddr *sap = (struct sockaddr *)&args->nfs_server.address;

	if (nfs_parse_mount_options((char *)options, args) == 0)
		return -EINVAL;

	if (!nfs_verify_server_address(sap))
		goto out_no_address;

	if (args->version == 4) {
#if IS_ENABLED(CONFIG_NFS_V4)
		if (args->nfs_server.protocol == XPRT_TRANSPORT_RDMA)
			port = NFS_RDMA_PORT;
		else
			port = NFS_PORT;
		max_namelen = NFS4_MAXNAMLEN;
		max_pathlen = NFS4_MAXPATHLEN;
		nfs_validate_transport_protocol(args);
		if (args->nfs_server.protocol == XPRT_TRANSPORT_UDP)
			goto out_invalid_transport_udp;
		nfs4_validate_mount_flags(args);
#else
		goto out_v4_not_compiled;
#endif /* CONFIG_NFS_V4 */
	} else {
		nfs_set_mount_transport_protocol(args);
		if (args->nfs_server.protocol == XPRT_TRANSPORT_RDMA)
			port = NFS_RDMA_PORT;
	}

	nfs_set_port(sap, &args->nfs_server.port, port);

	return nfs_parse_devname(dev_name,
				   &args->nfs_server.hostname,
				   max_namelen,
				   &args->nfs_server.export_path,
				   max_pathlen);

#if !IS_ENABLED(CONFIG_NFS_V4)
out_v4_not_compiled:
	dfprintk(MOUNT, "NFS: NFSv4 is not compiled into kernel\n");
	return -EPROTONOSUPPORT;
#else
out_invalid_transport_udp:
	dfprintk(MOUNT, "NFSv4: Unsupported transport protocol udp\n");
	return -EINVAL;
#endif /* !CONFIG_NFS_V4 */

out_no_address:
	dfprintk(MOUNT, "NFS: mount program didn't pass remote address\n");
	return -EINVAL;
}

#define NFS_REMOUNT_CMP_FLAGMASK ~(NFS_MOUNT_INTR \
		| NFS_MOUNT_SECURE \
		| NFS_MOUNT_TCP \
		| NFS_MOUNT_VER3 \
		| NFS_MOUNT_KERBEROS \
		| NFS_MOUNT_NONLM \
		| NFS_MOUNT_BROKEN_SUID \
		| NFS_MOUNT_STRICTLOCK \
		| NFS_MOUNT_LEGACY_INTERFACE)

#define NFS_MOUNT_CMP_FLAGMASK (NFS_REMOUNT_CMP_FLAGMASK & \
		~(NFS_MOUNT_UNSHARED | NFS_MOUNT_NORESVPORT))

static int
nfs_compare_remount_data(struct nfs_server *nfss,
			 struct nfs_parsed_mount_data *data)
{
	if ((data->flags ^ nfss->flags) & NFS_REMOUNT_CMP_FLAGMASK ||
	    data->rsize != nfss->rsize ||
	    data->wsize != nfss->wsize ||
	    data->version != nfss->nfs_client->rpc_ops->version ||
	    data->minorversion != nfss->nfs_client->cl_minorversion ||
	    data->retrans != nfss->client->cl_timeout->to_retries ||
	    !nfs_auth_info_match(&data->auth_info, nfss->client->cl_auth->au_flavor) ||
	    data->acregmin != nfss->acregmin / HZ ||
	    data->acregmax != nfss->acregmax / HZ ||
	    data->acdirmin != nfss->acdirmin / HZ ||
	    data->acdirmax != nfss->acdirmax / HZ ||
	    data->timeo != (10U * nfss->client->cl_timeout->to_initval / HZ) ||
	    data->nfs_server.port != nfss->port ||
	    data->nfs_server.addrlen != nfss->nfs_client->cl_addrlen ||
	    !rpc_cmp_addr((struct sockaddr *)&data->nfs_server.address,
			  (struct sockaddr *)&nfss->nfs_client->cl_addr))
		return -EINVAL;

	return 0;
}

int
nfs_remount(struct super_block *sb, int *flags, char *raw_data)
{
	int error;
	struct nfs_server *nfss = sb->s_fs_info;
	struct nfs_parsed_mount_data *data;
	struct nfs_mount_data *options = (struct nfs_mount_data *)raw_data;
	struct nfs4_mount_data *options4 = (struct nfs4_mount_data *)raw_data;
	u32 nfsvers = nfss->nfs_client->rpc_ops->version;

	sync_filesystem(sb);

	/*
	 * Userspace mount programs that send binary options generally send
	 * them populated with default values. We have no way to know which
	 * ones were explicitly specified. Fall back to legacy behavior and
	 * just return success.
	 */
	if ((nfsvers == 4 && (!options4 || options4->version == 1)) ||
	    (nfsvers <= 3 && (!options || (options->version >= 1 &&
					   options->version <= 6))))
		return 0;

	data = nfs_alloc_parsed_mount_data();
	if (data == NULL)
		return -ENOMEM;

	/* fill out struct with values from existing mount */
	data->flags = nfss->flags;
	data->rsize = nfss->rsize;
	data->wsize = nfss->wsize;
	data->retrans = nfss->client->cl_timeout->to_retries;
	data->selected_flavor = nfss->client->cl_auth->au_flavor;
	data->acregmin = nfss->acregmin / HZ;
	data->acregmax = nfss->acregmax / HZ;
	data->acdirmin = nfss->acdirmin / HZ;
	data->acdirmax = nfss->acdirmax / HZ;
	data->timeo = 10U * nfss->client->cl_timeout->to_initval / HZ;
	data->nfs_server.port = nfss->port;
	data->nfs_server.addrlen = nfss->nfs_client->cl_addrlen;
	data->version = nfsvers;
	data->minorversion = nfss->nfs_client->cl_minorversion;
	data->net = current->nsproxy->net_ns;
	memcpy(&data->nfs_server.address, &nfss->nfs_client->cl_addr,
		data->nfs_server.addrlen);

	/* overwrite those values with any that were specified */
	error = -EINVAL;
	if (!nfs_parse_mount_options((char *)options, data))
		goto out;

	/*
	 * noac is a special case. It implies -o sync, but that's not
	 * necessarily reflected in the mtab options. do_remount_sb
	 * will clear SB_SYNCHRONOUS if -o sync wasn't specified in the
	 * remount options, so we have to explicitly reset it.
	 */
	if (data->flags & NFS_MOUNT_NOAC)
		*flags |= SB_SYNCHRONOUS;

	/* compare new mount options with old ones */
	error = nfs_compare_remount_data(nfss, data);
	if (!error)
		error = security_sb_remount(sb, data->lsm_opts);
out:
	nfs_free_parsed_mount_data(data);
	return error;
}
EXPORT_SYMBOL_GPL(nfs_remount);

/*
 * Initialise the common bits of the superblock
 */
static void nfs_initialise_sb(struct super_block *sb)
{
	struct nfs_server *server = NFS_SB(sb);

	sb->s_magic = NFS_SUPER_MAGIC;

	/* We probably want something more informative here */
	snprintf(sb->s_id, sizeof(sb->s_id),
		 "%u:%u", MAJOR(sb->s_dev), MINOR(sb->s_dev));

	if (sb->s_blocksize == 0)
		sb->s_blocksize = nfs_block_bits(server->wsize,
						 &sb->s_blocksize_bits);

	nfs_super_set_maxbytes(sb, server->maxfilesize);
}

/*
 * Finish setting up an NFS2/3 superblock
 */
void nfs_fill_super(struct super_block *sb, struct nfs_mount_info *mount_info)
{
	struct nfs_parsed_mount_data *data = mount_info->parsed;
	struct nfs_server *server = NFS_SB(sb);

	sb->s_blocksize_bits = 0;
	sb->s_blocksize = 0;
	sb->s_xattr = server->nfs_client->cl_nfs_mod->xattr;
	sb->s_op = server->nfs_client->cl_nfs_mod->sops;
	if (data && data->bsize)
		sb->s_blocksize = nfs_block_size(data->bsize, &sb->s_blocksize_bits);

	if (server->nfs_client->rpc_ops->version != 2) {
		/* The VFS shouldn't apply the umask to mode bits. We will do
		 * so ourselves when necessary.
		 */
		sb->s_flags |= SB_POSIXACL;
		sb->s_time_gran = 1;
		sb->s_export_op = &nfs_export_ops;
	}

 	nfs_initialise_sb(sb);
}
EXPORT_SYMBOL_GPL(nfs_fill_super);

/*
 * Finish setting up a cloned NFS2/3/4 superblock
 */
static void nfs_clone_super(struct super_block *sb,
			    struct nfs_mount_info *mount_info)
{
	const struct super_block *old_sb = mount_info->cloned->sb;
	struct nfs_server *server = NFS_SB(sb);

	sb->s_blocksize_bits = old_sb->s_blocksize_bits;
	sb->s_blocksize = old_sb->s_blocksize;
	sb->s_maxbytes = old_sb->s_maxbytes;
	sb->s_xattr = old_sb->s_xattr;
	sb->s_op = old_sb->s_op;
	sb->s_time_gran = 1;
	sb->s_export_op = old_sb->s_export_op;

	if (server->nfs_client->rpc_ops->version != 2) {
		/* The VFS shouldn't apply the umask to mode bits. We will do
		 * so ourselves when necessary.
		 */
		sb->s_flags |= SB_POSIXACL;
	}

 	nfs_initialise_sb(sb);
}

static int nfs_compare_mount_options(const struct super_block *s, const struct nfs_server *b, int flags)
{
	const struct nfs_server *a = s->s_fs_info;
	const struct rpc_clnt *clnt_a = a->client;
	const struct rpc_clnt *clnt_b = b->client;

	if ((s->s_flags & NFS_MS_MASK) != (flags & NFS_MS_MASK))
		goto Ebusy;
	if (a->nfs_client != b->nfs_client)
		goto Ebusy;
	if ((a->flags ^ b->flags) & NFS_MOUNT_CMP_FLAGMASK)
		goto Ebusy;
	if (a->wsize != b->wsize)
		goto Ebusy;
	if (a->rsize != b->rsize)
		goto Ebusy;
	if (a->acregmin != b->acregmin)
		goto Ebusy;
	if (a->acregmax != b->acregmax)
		goto Ebusy;
	if (a->acdirmin != b->acdirmin)
		goto Ebusy;
	if (a->acdirmax != b->acdirmax)
		goto Ebusy;
	if (clnt_a->cl_auth->au_flavor != clnt_b->cl_auth->au_flavor)
		goto Ebusy;
	return 1;
Ebusy:
	return 0;
}

struct nfs_sb_mountdata {
	struct nfs_server *server;
	int mntflags;
};

static int nfs_set_super(struct super_block *s, void *data)
{
	struct nfs_sb_mountdata *sb_mntdata = data;
	struct nfs_server *server = sb_mntdata->server;
	int ret;

	s->s_flags = sb_mntdata->mntflags;
	s->s_fs_info = server;
	s->s_d_op = server->nfs_client->rpc_ops->dentry_ops;
	ret = set_anon_super(s, server);
	if (ret == 0)
		server->s_dev = s->s_dev;
	return ret;
}

static int nfs_compare_super_address(struct nfs_server *server1,
				     struct nfs_server *server2)
{
	struct sockaddr *sap1, *sap2;
	struct rpc_xprt *xprt1 = server1->client->cl_xprt;
	struct rpc_xprt *xprt2 = server2->client->cl_xprt;

	if (!net_eq(xprt1->xprt_net, xprt2->xprt_net))
		return 0;

	sap1 = (struct sockaddr *)&server1->nfs_client->cl_addr;
	sap2 = (struct sockaddr *)&server2->nfs_client->cl_addr;

	if (sap1->sa_family != sap2->sa_family)
		return 0;

	switch (sap1->sa_family) {
	case AF_INET: {
		struct sockaddr_in *sin1 = (struct sockaddr_in *)sap1;
		struct sockaddr_in *sin2 = (struct sockaddr_in *)sap2;
		if (sin1->sin_addr.s_addr != sin2->sin_addr.s_addr)
			return 0;
		if (sin1->sin_port != sin2->sin_port)
			return 0;
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 *sin1 = (struct sockaddr_in6 *)sap1;
		struct sockaddr_in6 *sin2 = (struct sockaddr_in6 *)sap2;
		if (!ipv6_addr_equal(&sin1->sin6_addr, &sin2->sin6_addr))
			return 0;
		if (sin1->sin6_port != sin2->sin6_port)
			return 0;
		break;
	}
	default:
		return 0;
	}

	return 1;
}

static int nfs_compare_super(struct super_block *sb, void *data)
{
	struct nfs_sb_mountdata *sb_mntdata = data;
	struct nfs_server *server = sb_mntdata->server, *old = NFS_SB(sb);
	int mntflags = sb_mntdata->mntflags;

	if (!nfs_compare_super_address(old, server))
		return 0;
	/* Note: NFS_MOUNT_UNSHARED == NFS4_MOUNT_UNSHARED */
	if (old->flags & NFS_MOUNT_UNSHARED)
		return 0;
	if (memcmp(&old->fsid, &server->fsid, sizeof(old->fsid)) != 0)
		return 0;
	return nfs_compare_mount_options(sb, server, mntflags);
}

#ifdef CONFIG_NFS_FSCACHE
static void nfs_get_cache_cookie(struct super_block *sb,
				 struct nfs_parsed_mount_data *parsed,
				 struct nfs_clone_mount *cloned)
{
	struct nfs_server *nfss = NFS_SB(sb);
	char *uniq = NULL;
	int ulen = 0;

	nfss->fscache_key = NULL;
	nfss->fscache = NULL;

	if (parsed) {
		if (!(parsed->options & NFS_OPTION_FSCACHE))
			return;
		if (parsed->fscache_uniq) {
			uniq = parsed->fscache_uniq;
			ulen = strlen(parsed->fscache_uniq);
		}
	} else if (cloned) {
		struct nfs_server *mnt_s = NFS_SB(cloned->sb);
		if (!(mnt_s->options & NFS_OPTION_FSCACHE))
			return;
		if (mnt_s->fscache_key) {
			uniq = mnt_s->fscache_key->key.uniquifier;
			ulen = mnt_s->fscache_key->key.uniq_len;
		};
	} else
		return;

	nfs_fscache_get_super_cookie(sb, uniq, ulen);
}
#else
static void nfs_get_cache_cookie(struct super_block *sb,
				 struct nfs_parsed_mount_data *parsed,
				 struct nfs_clone_mount *cloned)
{
}
#endif

int nfs_set_sb_security(struct super_block *s, struct dentry *mntroot,
			struct nfs_mount_info *mount_info)
{
	int error;
	unsigned long kflags = 0, kflags_out = 0;
	if (NFS_SB(s)->caps & NFS_CAP_SECURITY_LABEL)
		kflags |= SECURITY_LSM_NATIVE_LABELS;

	error = security_sb_set_mnt_opts(s, mount_info->parsed->lsm_opts,
						kflags, &kflags_out);
	if (error)
		goto err;

	if (NFS_SB(s)->caps & NFS_CAP_SECURITY_LABEL &&
		!(kflags_out & SECURITY_LSM_NATIVE_LABELS))
		NFS_SB(s)->caps &= ~NFS_CAP_SECURITY_LABEL;
err:
	return error;
}
EXPORT_SYMBOL_GPL(nfs_set_sb_security);

int nfs_clone_sb_security(struct super_block *s, struct dentry *mntroot,
			  struct nfs_mount_info *mount_info)
{
	int error;
	unsigned long kflags = 0, kflags_out = 0;

	/* clone any lsm security options from the parent to the new sb */
	if (d_inode(mntroot)->i_op != NFS_SB(s)->nfs_client->rpc_ops->dir_inode_ops)
		return -ESTALE;

	if (NFS_SB(s)->caps & NFS_CAP_SECURITY_LABEL)
		kflags |= SECURITY_LSM_NATIVE_LABELS;

	error = security_sb_clone_mnt_opts(mount_info->cloned->sb, s, kflags,
			&kflags_out);
	if (error)
		return error;

	if (NFS_SB(s)->caps & NFS_CAP_SECURITY_LABEL &&
		!(kflags_out & SECURITY_LSM_NATIVE_LABELS))
		NFS_SB(s)->caps &= ~NFS_CAP_SECURITY_LABEL;
	return 0;
}
EXPORT_SYMBOL_GPL(nfs_clone_sb_security);

struct dentry *nfs_fs_mount_common(struct nfs_server *server,
				   int flags, const char *dev_name,
				   struct nfs_mount_info *mount_info,
				   struct nfs_subversion *nfs_mod)
{
	struct super_block *s;
	struct dentry *mntroot = ERR_PTR(-ENOMEM);
	int (*compare_super)(struct super_block *, void *) = nfs_compare_super;
	struct nfs_sb_mountdata sb_mntdata = {
		.mntflags = flags,
		.server = server,
	};
	int error;

	if (server->flags & NFS_MOUNT_UNSHARED)
		compare_super = NULL;

	/* -o noac implies -o sync */
	if (server->flags & NFS_MOUNT_NOAC)
		sb_mntdata.mntflags |= SB_SYNCHRONOUS;

	if (mount_info->cloned != NULL && mount_info->cloned->sb != NULL)
		if (mount_info->cloned->sb->s_flags & SB_SYNCHRONOUS)
			sb_mntdata.mntflags |= SB_SYNCHRONOUS;

	/* Get a superblock - note that we may end up sharing one that already exists */
	s = sget(nfs_mod->nfs_fs, compare_super, nfs_set_super, flags, &sb_mntdata);
	if (IS_ERR(s)) {
		mntroot = ERR_CAST(s);
		goto out_err_nosb;
	}

	if (s->s_fs_info != server) {
		nfs_free_server(server);
		server = NULL;
	} else {
		error = super_setup_bdi_name(s, "%u:%u", MAJOR(server->s_dev),
					     MINOR(server->s_dev));
		if (error) {
			mntroot = ERR_PTR(error);
			goto error_splat_super;
		}
		s->s_bdi->ra_pages = server->rpages * NFS_MAX_READAHEAD;
		server->super = s;
	}

	if (!s->s_root) {
		/* initial superblock/root creation */
		mount_info->fill_super(s, mount_info);
		nfs_get_cache_cookie(s, mount_info->parsed, mount_info->cloned);
		if (!(server->flags & NFS_MOUNT_UNSHARED))
			s->s_iflags |= SB_I_MULTIROOT;
	}

	mntroot = nfs_get_root(s, mount_info->mntfh, dev_name);
	if (IS_ERR(mntroot))
		goto error_splat_super;

	error = mount_info->set_security(s, mntroot, mount_info);
	if (error)
		goto error_splat_root;

	s->s_flags |= SB_ACTIVE;

out:
	return mntroot;

out_err_nosb:
	nfs_free_server(server);
	goto out;

error_splat_root:
	dput(mntroot);
	mntroot = ERR_PTR(error);
error_splat_super:
	deactivate_locked_super(s);
	goto out;
}
EXPORT_SYMBOL_GPL(nfs_fs_mount_common);

struct dentry *nfs_fs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data)
{
	struct nfs_mount_info mount_info = {
		.fill_super = nfs_fill_super,
		.set_security = nfs_set_sb_security,
	};
	struct dentry *mntroot = ERR_PTR(-ENOMEM);
	struct nfs_subversion *nfs_mod;
	int error;

	mount_info.parsed = nfs_alloc_parsed_mount_data();
	mount_info.mntfh = nfs_alloc_fhandle();
	if (mount_info.parsed == NULL || mount_info.mntfh == NULL)
		goto out;

	/* Validate the mount data */
	error = nfs_validate_mount_data(fs_type, raw_data, mount_info.parsed, mount_info.mntfh, dev_name);
	if (error == NFS_TEXT_DATA)
		error = nfs_validate_text_mount_data(raw_data, mount_info.parsed, dev_name);
	if (error < 0) {
		mntroot = ERR_PTR(error);
		goto out;
	}

	nfs_mod = get_nfs_version(mount_info.parsed->version);
	if (IS_ERR(nfs_mod)) {
		mntroot = ERR_CAST(nfs_mod);
		goto out;
	}

	mntroot = nfs_mod->rpc_ops->try_mount(flags, dev_name, &mount_info, nfs_mod);

	put_nfs_version(nfs_mod);
out:
	nfs_free_parsed_mount_data(mount_info.parsed);
	nfs_free_fhandle(mount_info.mntfh);
	return mntroot;
}
EXPORT_SYMBOL_GPL(nfs_fs_mount);

/*
 * Destroy an NFS2/3 superblock
 */
void nfs_kill_super(struct super_block *s)
{
	struct nfs_server *server = NFS_SB(s);
	dev_t dev = s->s_dev;

	generic_shutdown_super(s);

	nfs_fscache_release_super_cookie(s);

	nfs_free_server(server);
	free_anon_bdev(dev);
}
EXPORT_SYMBOL_GPL(nfs_kill_super);

/*
 * Clone an NFS2/3/4 server record on xdev traversal (FSID-change)
 */
static struct dentry *
nfs_xdev_mount(struct file_system_type *fs_type, int flags,
		const char *dev_name, void *raw_data)
{
	struct nfs_clone_mount *data = raw_data;
	struct nfs_mount_info mount_info = {
		.fill_super = nfs_clone_super,
		.set_security = nfs_clone_sb_security,
		.cloned = data,
	};
	struct nfs_server *server;
	struct dentry *mntroot = ERR_PTR(-ENOMEM);
	struct nfs_subversion *nfs_mod = NFS_SB(data->sb)->nfs_client->cl_nfs_mod;

	dprintk("--> nfs_xdev_mount()\n");

	mount_info.mntfh = mount_info.cloned->fh;

	/* create a new volume representation */
	server = nfs_mod->rpc_ops->clone_server(NFS_SB(data->sb), data->fh, data->fattr, data->authflavor);

	if (IS_ERR(server))
		mntroot = ERR_CAST(server);
	else
		mntroot = nfs_fs_mount_common(server, flags,
				dev_name, &mount_info, nfs_mod);

	dprintk("<-- nfs_xdev_mount() = %ld\n",
			IS_ERR(mntroot) ? PTR_ERR(mntroot) : 0L);
	return mntroot;
}

#if IS_ENABLED(CONFIG_NFS_V4)

static void nfs4_validate_mount_flags(struct nfs_parsed_mount_data *args)
{
	args->flags &= ~(NFS_MOUNT_NONLM|NFS_MOUNT_NOACL|NFS_MOUNT_VER3|
			 NFS_MOUNT_LOCAL_FLOCK|NFS_MOUNT_LOCAL_FCNTL);
}

/*
 * Validate NFSv4 mount options
 */
static int nfs4_validate_mount_data(void *options,
				    struct nfs_parsed_mount_data *args,
				    const char *dev_name)
{
	struct sockaddr *sap = (struct sockaddr *)&args->nfs_server.address;
	struct nfs4_mount_data *data = (struct nfs4_mount_data *)options;
	char *c;

	if (data == NULL)
		goto out_no_data;

	args->version = 4;

	switch (data->version) {
	case 1:
		if (data->host_addrlen > sizeof(args->nfs_server.address))
			goto out_no_address;
		if (data->host_addrlen == 0)
			goto out_no_address;
		args->nfs_server.addrlen = data->host_addrlen;
		if (copy_from_user(sap, data->host_addr, data->host_addrlen))
			return -EFAULT;
		if (!nfs_verify_server_address(sap))
			goto out_no_address;
		args->nfs_server.port = ntohs(((struct sockaddr_in *)sap)->sin_port);

		if (data->auth_flavourlen) {
			rpc_authflavor_t pseudoflavor;
			if (data->auth_flavourlen > 1)
				goto out_inval_auth;
			if (copy_from_user(&pseudoflavor,
					   data->auth_flavours,
					   sizeof(pseudoflavor)))
				return -EFAULT;
			args->selected_flavor = pseudoflavor;
		} else
			args->selected_flavor = RPC_AUTH_UNIX;

		c = strndup_user(data->hostname.data, NFS4_MAXNAMLEN);
		if (IS_ERR(c))
			return PTR_ERR(c);
		args->nfs_server.hostname = c;

		c = strndup_user(data->mnt_path.data, NFS4_MAXPATHLEN);
		if (IS_ERR(c))
			return PTR_ERR(c);
		args->nfs_server.export_path = c;
		dfprintk(MOUNT, "NFS: MNTPATH: '%s'\n", c);

		c = strndup_user(data->client_addr.data, 16);
		if (IS_ERR(c))
			return PTR_ERR(c);
		args->client_address = c;

		/*
		 * Translate to nfs_parsed_mount_data, which nfs4_fill_super
		 * can deal with.
		 */

		args->flags	= data->flags & NFS4_MOUNT_FLAGMASK;
		args->rsize	= data->rsize;
		args->wsize	= data->wsize;
		args->timeo	= data->timeo;
		args->retrans	= data->retrans;
		args->acregmin	= data->acregmin;
		args->acregmax	= data->acregmax;
		args->acdirmin	= data->acdirmin;
		args->acdirmax	= data->acdirmax;
		args->nfs_server.protocol = data->proto;
		nfs_validate_transport_protocol(args);
		if (args->nfs_server.protocol == XPRT_TRANSPORT_UDP)
			goto out_invalid_transport_udp;

		break;
	default:
		return NFS_TEXT_DATA;
	}

	return 0;

out_no_data:
	dfprintk(MOUNT, "NFS4: mount program didn't pass any mount data\n");
	return -EINVAL;

out_inval_auth:
	dfprintk(MOUNT, "NFS4: Invalid number of RPC auth flavours %d\n",
		 data->auth_flavourlen);
	return -EINVAL;

out_no_address:
	dfprintk(MOUNT, "NFS4: mount program didn't pass remote address\n");
	return -EINVAL;

out_invalid_transport_udp:
	dfprintk(MOUNT, "NFSv4: Unsupported transport protocol udp\n");
	return -EINVAL;
}

/*
 * NFS v4 module parameters need to stay in the
 * NFS client for backwards compatibility
 */
unsigned int nfs_callback_set_tcpport;
unsigned short nfs_callback_nr_threads;
/* Default cache timeout is 10 minutes */
unsigned int nfs_idmap_cache_timeout = 600;
/* Turn off NFSv4 uid/gid mapping when using AUTH_SYS */
bool nfs4_disable_idmapping = true;
unsigned short max_session_slots = NFS4_DEF_SLOT_TABLE_SIZE;
unsigned short max_session_cb_slots = NFS4_DEF_CB_SLOT_TABLE_SIZE;
unsigned short send_implementation_id = 1;
char nfs4_client_id_uniquifier[NFS4_CLIENT_ID_UNIQ_LEN] = "";
bool recover_lost_locks = false;

EXPORT_SYMBOL_GPL(nfs_callback_nr_threads);
EXPORT_SYMBOL_GPL(nfs_callback_set_tcpport);
EXPORT_SYMBOL_GPL(nfs_idmap_cache_timeout);
EXPORT_SYMBOL_GPL(nfs4_disable_idmapping);
EXPORT_SYMBOL_GPL(max_session_slots);
EXPORT_SYMBOL_GPL(max_session_cb_slots);
EXPORT_SYMBOL_GPL(send_implementation_id);
EXPORT_SYMBOL_GPL(nfs4_client_id_uniquifier);
EXPORT_SYMBOL_GPL(recover_lost_locks);

#define NFS_CALLBACK_MAXPORTNR (65535U)

static int param_set_portnr(const char *val, const struct kernel_param *kp)
{
	unsigned long num;
	int ret;

	if (!val)
		return -EINVAL;
	ret = kstrtoul(val, 0, &num);
	if (ret || num > NFS_CALLBACK_MAXPORTNR)
		return -EINVAL;
	*((unsigned int *)kp->arg) = num;
	return 0;
}
static const struct kernel_param_ops param_ops_portnr = {
	.set = param_set_portnr,
	.get = param_get_uint,
};
#define param_check_portnr(name, p) __param_check(name, p, unsigned int);

module_param_named(callback_tcpport, nfs_callback_set_tcpport, portnr, 0644);
module_param_named(callback_nr_threads, nfs_callback_nr_threads, ushort, 0644);
MODULE_PARM_DESC(callback_nr_threads, "Number of threads that will be "
		"assigned to the NFSv4 callback channels.");
module_param(nfs_idmap_cache_timeout, int, 0644);
module_param(nfs4_disable_idmapping, bool, 0644);
module_param_string(nfs4_unique_id, nfs4_client_id_uniquifier,
			NFS4_CLIENT_ID_UNIQ_LEN, 0600);
MODULE_PARM_DESC(nfs4_disable_idmapping,
		"Turn off NFSv4 idmapping when using 'sec=sys'");
module_param(max_session_slots, ushort, 0644);
MODULE_PARM_DESC(max_session_slots, "Maximum number of outstanding NFSv4.1 "
		"requests the client will negotiate");
module_param(max_session_cb_slots, ushort, 0644);
MODULE_PARM_DESC(max_session_cb_slots, "Maximum number of parallel NFSv4.1 "
		"callbacks the client will process for a given server");
module_param(send_implementation_id, ushort, 0644);
MODULE_PARM_DESC(send_implementation_id,
		"Send implementation ID with NFSv4.1 exchange_id");
MODULE_PARM_DESC(nfs4_unique_id, "nfs_client_id4 uniquifier string");

module_param(recover_lost_locks, bool, 0644);
MODULE_PARM_DESC(recover_lost_locks,
		 "If the server reports that a lock might be lost, "
		 "try to recover it risking data corruption.");


#endif /* CONFIG_NFS_V4 */
