// SPDX-License-Identifier: GPL-2.0-only
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
#include <linux/sched.h>
#include <linux/slab.h>
#include <net/ipv6.h>
#include <linux/netdevice.h>
#include <linux/nfs_xdr.h>
#include <linux/magic.h>
#include <linux/parser.h>
#include <linux/nsproxy.h>
#include <linux/rcupdate.h>

#include <linux/uaccess.h>
#include <linux/nfs_ssc.h>

#include <uapi/linux/tls.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"
#include "iostat.h"
#include "internal.h"
#include "fscache.h"
#include "nfs4session.h"
#include "pnfs.h"
#include "nfs.h"
#include "netns.h"
#include "sysfs.h"
#include "nfs4idmap.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

const struct super_operations nfs_sops = {
	.alloc_inode	= nfs_alloc_inode,
	.free_inode	= nfs_free_inode,
	.write_inode	= nfs_write_inode,
	.drop_inode	= nfs_drop_inode,
	.statfs		= nfs_statfs,
	.evict_inode	= nfs_evict_inode,
	.umount_begin	= nfs_umount_begin,
	.show_options	= nfs_show_options,
	.show_devname	= nfs_show_devname,
	.show_path	= nfs_show_path,
	.show_stats	= nfs_show_stats,
};
EXPORT_SYMBOL_GPL(nfs_sops);

#ifdef CONFIG_NFS_V4_2
static const struct nfs_ssc_client_ops nfs_ssc_clnt_ops_tbl = {
	.sco_sb_deactive = nfs_sb_deactive,
};
#endif

#if IS_ENABLED(CONFIG_NFS_V4)
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

#ifdef CONFIG_NFS_V4_2
static void nfs_ssc_register_ops(void)
{
	nfs_ssc_register(&nfs_ssc_clnt_ops_tbl);
}

static void nfs_ssc_unregister_ops(void)
{
	nfs_ssc_unregister(&nfs_ssc_clnt_ops_tbl);
}
#endif /* CONFIG_NFS_V4_2 */

static struct shrinker *acl_shrinker;

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

	acl_shrinker = shrinker_alloc(0, "nfs-acl");
	if (!acl_shrinker) {
		ret = -ENOMEM;
		goto error_3;
	}

	acl_shrinker->count_objects = nfs_access_cache_count;
	acl_shrinker->scan_objects = nfs_access_cache_scan;

	shrinker_register(acl_shrinker);

#ifdef CONFIG_NFS_V4_2
	nfs_ssc_register_ops();
#endif
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
	shrinker_free(acl_shrinker);
	nfs_unregister_sysctl();
	unregister_nfs4_fs();
#ifdef CONFIG_NFS_V4_2
	nfs_ssc_unregister_ops();
#endif
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

static int __nfs_list_for_each_server(struct list_head *head,
		int (*fn)(struct nfs_server *, void *),
		void *data)
{
	struct nfs_server *server, *last = NULL;
	int ret = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(server, head, client_link) {
		if (!(server->super && nfs_sb_active(server->super)))
			continue;
		rcu_read_unlock();
		if (last)
			nfs_sb_deactive(last->super);
		last = server;
		ret = fn(server, data);
		if (ret)
			goto out;
		cond_resched();
		rcu_read_lock();
	}
	rcu_read_unlock();
out:
	if (last)
		nfs_sb_deactive(last->super);
	return ret;
}

int nfs_client_for_each_server(struct nfs_client *clp,
		int (*fn)(struct nfs_server *, void *),
		void *data)
{
	return __nfs_list_for_each_server(&clp->cl_superblocks, fn, data);
}
EXPORT_SYMBOL_GPL(nfs_client_for_each_server);

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
		nfs_zap_caches(d_inode(pd_dentry));
		dput(pd_dentry);
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
			seq_puts(m, ",mountaddr=unspecified");
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
		{ NFS_MOUNT_SOFT, ",soft", "" },
		{ NFS_MOUNT_SOFTERR, ",softerr", "" },
		{ NFS_MOUNT_SOFTREVAL, ",softreval", "" },
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
	if (!(nfss->flags & (NFS_MOUNT_SOFT|NFS_MOUNT_SOFTERR)))
			seq_puts(m, ",hard");
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
	if (clp->cl_nconnect > 0)
		seq_printf(m, ",nconnect=%u", clp->cl_nconnect);
	if (version == 4) {
		if (clp->cl_max_connect > 1)
			seq_printf(m, ",max_connect=%u", clp->cl_max_connect);
		if (nfss->port != NFS_PORT)
			seq_printf(m, ",port=%u", nfss->port);
	} else
		if (nfss->port)
			seq_printf(m, ",port=%u", nfss->port);

	seq_printf(m, ",timeo=%lu", 10U * nfss->client->cl_timeout->to_initval / HZ);
	seq_printf(m, ",retrans=%u", nfss->client->cl_timeout->to_retries);
	seq_printf(m, ",sec=%s", nfs_pseudoflavour_to_name(nfss->client->cl_auth->au_flavor));
	switch (clp->cl_xprtsec.policy) {
	case RPC_XPRTSEC_TLS_ANON:
		seq_puts(m, ",xprtsec=tls");
		break;
	case RPC_XPRTSEC_TLS_X509:
		seq_puts(m, ",xprtsec=mtls");
		break;
	default:
		break;
	}

	if (version != 4)
		nfs_show_mountd_options(m, nfss, showdefaults);
	else
		nfs_show_nfsv4_options(m, nfss, showdefaults);

	if (nfss->options & NFS_OPTION_FSCACHE) {
#ifdef CONFIG_NFS_FSCACHE
		if (nfss->fscache_uniq)
			seq_printf(m, ",fsc=%s", nfss->fscache_uniq);
		else
			seq_puts(m, ",fsc");
#else
		seq_puts(m, ",fsc");
#endif
	}

	if (nfss->options & NFS_OPTION_MIGRATION)
		seq_puts(m, ",migration");

	if (nfss->flags & NFS_MOUNT_LOOKUP_CACHE_NONEG) {
		if (nfss->flags & NFS_MOUNT_LOOKUP_CACHE_NONE)
			seq_puts(m, ",lookupcache=none");
		else
			seq_puts(m, ",lookupcache=pos");
	}

	local_flock = nfss->flags & NFS_MOUNT_LOCAL_FLOCK;
	local_fcntl = nfss->flags & NFS_MOUNT_LOCAL_FCNTL;

	if (!local_flock && !local_fcntl)
		seq_puts(m, ",local_lock=none");
	else if (local_flock && local_fcntl)
		seq_puts(m, ",local_lock=all");
	else if (local_flock)
		seq_puts(m, ",local_lock=flock");
	else
		seq_puts(m, ",local_lock=posix");

	if (nfss->flags & NFS_MOUNT_NO_ALIGNWRITE)
		seq_puts(m, ",noalignwrite");

	if (nfss->flags & NFS_MOUNT_WRITE_EAGER) {
		if (nfss->flags & NFS_MOUNT_WRITE_WAIT)
			seq_puts(m, ",write=wait");
		else
			seq_puts(m, ",write=eager");
	}
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
static void show_lease(struct seq_file *m, struct nfs_server *server)
{
	struct nfs_client *clp = server->nfs_client;
	unsigned long expire;

	seq_printf(m, ",lease_time=%ld", clp->cl_lease_time / HZ);
	expire = clp->cl_last_renewal + clp->cl_lease_time;
	seq_printf(m, ",lease_expired=%ld",
		   time_after(expire, jiffies) ?  0 : (jiffies - expire) / HZ);
}
#ifdef CONFIG_NFS_V4_1
static void show_sessions(struct seq_file *m, struct nfs_server *server)
{
	if (nfs4_has_session(server->nfs_client))
		seq_puts(m, ",sessions");
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
	seq_puts(m, "\n\topts:\t");
	seq_puts(m, sb_rdonly(root->d_sb) ? "ro" : "rw");
	seq_puts(m, root->d_sb->s_flags & SB_SYNCHRONOUS ? ",sync" : "");
	seq_puts(m, root->d_sb->s_flags & SB_NOATIME ? ",noatime" : "");
	seq_puts(m, root->d_sb->s_flags & SB_NODIRATIME ? ",nodiratime" : "");
	nfs_show_mount_options(m, nfss, 1);

	seq_printf(m, "\n\tage:\t%lu", (jiffies - nfss->mount_time) / HZ);

	show_implementation_id(m, nfss);

	seq_puts(m, "\n\tcaps:\t");
	seq_printf(m, "caps=0x%x", nfss->caps);
	seq_printf(m, ",wtmult=%u", nfss->wtmult);
	seq_printf(m, ",dtsize=%u", nfss->dtsize);
	seq_printf(m, ",bsize=%u", nfss->bsize);
	seq_printf(m, ",namlen=%u", nfss->namelen);

#if IS_ENABLED(CONFIG_NFS_V4)
	if (nfss->nfs_client->rpc_ops->version == 4) {
		seq_puts(m, "\n\tnfsv4:\t");
		seq_printf(m, "bm0=0x%x", nfss->attr_bitmask[0]);
		seq_printf(m, ",bm1=0x%x", nfss->attr_bitmask[1]);
		seq_printf(m, ",bm2=0x%x", nfss->attr_bitmask[2]);
		seq_printf(m, ",acl=0x%x", nfss->acl_bitmask);
		show_sessions(m, nfss);
		show_pnfs(m, nfss);
		show_lease(m, nfss);
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

		preempt_enable();
	}

	seq_puts(m, "\n\tevents:\t");
	for (i = 0; i < __NFSIOS_COUNTSMAX; i++)
		seq_printf(m, "%lu ", totals.events[i]);
	seq_puts(m, "\n\tbytes:\t");
	for (i = 0; i < __NFSIOS_BYTESMAX; i++)
		seq_printf(m, "%Lu ", totals.bytes[i]);
	seq_putc(m, '\n');

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
 * Ensure that a specified authtype in ctx->auth_info is supported by
 * the server. Returns 0 and sets ctx->selected_flavor if it's ok, and
 * -EACCES if not.
 */
static int nfs_verify_authflavors(struct nfs_fs_context *ctx,
				  rpc_authflavor_t *server_authlist,
				  unsigned int count)
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

		if (nfs_auth_info_match(&ctx->auth_info, flavor))
			goto out;

		if (flavor == RPC_AUTH_NULL)
			found_auth_null = true;
	}

	if (found_auth_null) {
		flavor = ctx->auth_info.flavors[0];
		goto out;
	}

	dfprintk(MOUNT,
		 "NFS: specified auth flavors not supported by server\n");
	return -EACCES;

out:
	ctx->selected_flavor = flavor;
	dfprintk(MOUNT, "NFS: using auth flavor %u\n", ctx->selected_flavor);
	return 0;
}

/*
 * Use the remote server's MOUNT service to request the NFS file handle
 * corresponding to the provided path.
 */
static int nfs_request_mount(struct fs_context *fc,
			     struct nfs_fh *root_fh,
			     rpc_authflavor_t *server_authlist,
			     unsigned int *server_authlist_len)
{
	struct nfs_fs_context *ctx = nfs_fc2context(fc);
	struct nfs_mount_request request = {
		.sap		= &ctx->mount_server._address,
		.dirpath	= ctx->nfs_server.export_path,
		.protocol	= ctx->mount_server.protocol,
		.fh		= root_fh,
		.noresvport	= ctx->flags & NFS_MOUNT_NORESVPORT,
		.auth_flav_len	= server_authlist_len,
		.auth_flavs	= server_authlist,
		.net		= fc->net_ns,
	};
	int status;

	if (ctx->mount_server.version == 0) {
		switch (ctx->version) {
			default:
				ctx->mount_server.version = NFS_MNT3_VERSION;
				break;
			case 2:
				ctx->mount_server.version = NFS_MNT_VERSION;
		}
	}
	request.version = ctx->mount_server.version;

	if (ctx->mount_server.hostname)
		request.hostname = ctx->mount_server.hostname;
	else
		request.hostname = ctx->nfs_server.hostname;

	/*
	 * Construct the mount server's address.
	 */
	if (ctx->mount_server.address.sa_family == AF_UNSPEC) {
		memcpy(request.sap, &ctx->nfs_server._address,
		       ctx->nfs_server.addrlen);
		ctx->mount_server.addrlen = ctx->nfs_server.addrlen;
	}
	request.salen = ctx->mount_server.addrlen;
	nfs_set_port(request.sap, &ctx->mount_server.port, 0);

	/*
	 * Now ask the mount server to map our export path
	 * to a file handle.
	 */
	if ((request.protocol == XPRT_TRANSPORT_UDP) ==
	    !(ctx->flags & NFS_MOUNT_TCP))
		/*
		 * NFS protocol and mount protocol are both UDP or neither UDP
		 * so timeouts are compatible.  Use NFS timeouts for MOUNT
		 */
		status = nfs_mount(&request, ctx->timeo, ctx->retrans);
	else
		status = nfs_mount(&request, NFS_UNSPEC_TIMEO, NFS_UNSPEC_RETRANS);
	if (status != 0) {
		dfprintk(MOUNT, "NFS: unable to mount server %s, error %d\n",
				request.hostname, status);
		return status;
	}

	return 0;
}

static struct nfs_server *nfs_try_mount_request(struct fs_context *fc)
{
	struct nfs_fs_context *ctx = nfs_fc2context(fc);
	int status;
	unsigned int i;
	bool tried_auth_unix = false;
	bool auth_null_in_list = false;
	struct nfs_server *server = ERR_PTR(-EACCES);
	rpc_authflavor_t authlist[NFS_MAX_SECFLAVORS];
	unsigned int authlist_len = ARRAY_SIZE(authlist);

	/* make sure 'nolock'/'lock' override the 'local_lock' mount option */
	if (ctx->lock_status) {
		if (ctx->lock_status == NFS_LOCK_NOLOCK) {
			ctx->flags |= NFS_MOUNT_NONLM;
			ctx->flags |= (NFS_MOUNT_LOCAL_FLOCK | NFS_MOUNT_LOCAL_FCNTL);
		} else {
			ctx->flags &= ~NFS_MOUNT_NONLM;
			ctx->flags &= ~(NFS_MOUNT_LOCAL_FLOCK | NFS_MOUNT_LOCAL_FCNTL);
		}
	}
	status = nfs_request_mount(fc, ctx->mntfh, authlist, &authlist_len);
	if (status)
		return ERR_PTR(status);

	/*
	 * Was a sec= authflavor specified in the options? First, verify
	 * whether the server supports it, and then just try to use it if so.
	 */
	if (ctx->auth_info.flavor_len > 0) {
		status = nfs_verify_authflavors(ctx, authlist, authlist_len);
		dfprintk(MOUNT, "NFS: using auth flavor %u\n",
			 ctx->selected_flavor);
		if (status)
			return ERR_PTR(status);
		return ctx->nfs_mod->rpc_ops->create_server(fc);
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
			break;
		}
		dfprintk(MOUNT, "NFS: attempting to use auth flavor %u\n", flavor);
		ctx->selected_flavor = flavor;
		server = ctx->nfs_mod->rpc_ops->create_server(fc);
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
	ctx->selected_flavor = RPC_AUTH_UNIX;
	return ctx->nfs_mod->rpc_ops->create_server(fc);
}

int nfs_try_get_tree(struct fs_context *fc)
{
	struct nfs_fs_context *ctx = nfs_fc2context(fc);

	if (ctx->need_mount)
		ctx->server = nfs_try_mount_request(fc);
	else
		ctx->server = ctx->nfs_mod->rpc_ops->create_server(fc);

	return nfs_get_tree_common(fc);
}
EXPORT_SYMBOL_GPL(nfs_try_get_tree);


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
			 struct nfs_fs_context *ctx)
{
	if ((ctx->flags ^ nfss->flags) & NFS_REMOUNT_CMP_FLAGMASK ||
	    ctx->rsize != nfss->rsize ||
	    ctx->wsize != nfss->wsize ||
	    ctx->version != nfss->nfs_client->rpc_ops->version ||
	    ctx->minorversion != nfss->nfs_client->cl_minorversion ||
	    ctx->retrans != nfss->client->cl_timeout->to_retries ||
	    !nfs_auth_info_match(&ctx->auth_info, nfss->client->cl_auth->au_flavor) ||
	    ctx->acregmin != nfss->acregmin / HZ ||
	    ctx->acregmax != nfss->acregmax / HZ ||
	    ctx->acdirmin != nfss->acdirmin / HZ ||
	    ctx->acdirmax != nfss->acdirmax / HZ ||
	    ctx->timeo != (10U * nfss->client->cl_timeout->to_initval / HZ) ||
	    (ctx->options & NFS_OPTION_FSCACHE) != (nfss->options & NFS_OPTION_FSCACHE) ||
	    ctx->nfs_server.port != nfss->port ||
	    ctx->nfs_server.addrlen != nfss->nfs_client->cl_addrlen ||
	    !rpc_cmp_addr((struct sockaddr *)&ctx->nfs_server.address,
			  (struct sockaddr *)&nfss->nfs_client->cl_addr))
		return -EINVAL;

	return 0;
}

int nfs_reconfigure(struct fs_context *fc)
{
	struct nfs_fs_context *ctx = nfs_fc2context(fc);
	struct super_block *sb = fc->root->d_sb;
	struct nfs_server *nfss = sb->s_fs_info;
	int ret;

	sync_filesystem(sb);

	/*
	 * Userspace mount programs that send binary options generally send
	 * them populated with default values. We have no way to know which
	 * ones were explicitly specified. Fall back to legacy behavior and
	 * just return success.
	 */
	if (ctx->skip_reconfig_option_check)
		return 0;

	/*
	 * noac is a special case. It implies -o sync, but that's not
	 * necessarily reflected in the mtab options. reconfigure_super
	 * will clear SB_SYNCHRONOUS if -o sync wasn't specified in the
	 * remount options, so we have to explicitly reset it.
	 */
	if (ctx->flags & NFS_MOUNT_NOAC) {
		fc->sb_flags |= SB_SYNCHRONOUS;
		fc->sb_flags_mask |= SB_SYNCHRONOUS;
	}

	/* compare new mount options with old ones */
	ret = nfs_compare_remount_data(nfss, ctx);
	if (ret)
		return ret;

	return nfs_probe_server(nfss, NFS_FH(d_inode(fc->root)));
}
EXPORT_SYMBOL_GPL(nfs_reconfigure);

/*
 * Finish setting up an NFS superblock
 */
static void nfs_fill_super(struct super_block *sb, struct nfs_fs_context *ctx)
{
	struct nfs_server *server = NFS_SB(sb);

	sb->s_blocksize_bits = 0;
	sb->s_blocksize = 0;
	sb->s_xattr = server->nfs_client->cl_nfs_mod->xattr;
	sb->s_op = server->nfs_client->cl_nfs_mod->sops;
	if (ctx->bsize)
		sb->s_blocksize = nfs_block_size(ctx->bsize, &sb->s_blocksize_bits);

	switch (server->nfs_client->rpc_ops->version) {
	case 2:
		sb->s_time_gran = 1000;
		sb->s_time_min = 0;
		sb->s_time_max = U32_MAX;
		break;
	case 3:
		/*
		 * The VFS shouldn't apply the umask to mode bits.
		 * We will do so ourselves when necessary.
		 */
		sb->s_flags |= SB_POSIXACL;
		sb->s_time_gran = 1;
		sb->s_time_min = 0;
		sb->s_time_max = U32_MAX;
		sb->s_export_op = &nfs_export_ops;
		break;
	case 4:
		sb->s_iflags |= SB_I_NOUMASK;
		sb->s_time_gran = 1;
		sb->s_time_min = S64_MIN;
		sb->s_time_max = S64_MAX;
		if (server->caps & NFS_CAP_ATOMIC_OPEN_V1)
			sb->s_export_op = &nfs_export_ops;
		break;
	}

	sb->s_magic = NFS_SUPER_MAGIC;

	/* We probably want something more informative here */
	snprintf(sb->s_id, sizeof(sb->s_id),
		 "%u:%u", MAJOR(sb->s_dev), MINOR(sb->s_dev));

	if (sb->s_blocksize == 0)
		sb->s_blocksize = nfs_block_bits(server->wsize,
						 &sb->s_blocksize_bits);

	nfs_super_set_maxbytes(sb, server->maxfilesize);
	nfs_sysfs_move_server_to_sb(sb);
	server->has_sec_mnt_opts = ctx->has_sec_mnt_opts;
}

static int nfs_compare_mount_options(const struct super_block *s, const struct nfs_server *b,
				     const struct fs_context *fc)
{
	const struct nfs_server *a = s->s_fs_info;
	const struct rpc_clnt *clnt_a = a->client;
	const struct rpc_clnt *clnt_b = b->client;

	if ((s->s_flags & NFS_SB_MASK) != (fc->sb_flags & NFS_SB_MASK))
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

static int nfs_set_super(struct super_block *s, struct fs_context *fc)
{
	struct nfs_server *server = fc->s_fs_info;
	int ret;

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

static int nfs_compare_userns(const struct nfs_server *old,
		const struct nfs_server *new)
{
	const struct user_namespace *oldns = &init_user_ns;
	const struct user_namespace *newns = &init_user_ns;

	if (old->client && old->client->cl_cred)
		oldns = old->client->cl_cred->user_ns;
	if (new->client && new->client->cl_cred)
		newns = new->client->cl_cred->user_ns;
	if (oldns != newns)
		return 0;
	return 1;
}

static int nfs_compare_super(struct super_block *sb, struct fs_context *fc)
{
	struct nfs_server *server = fc->s_fs_info, *old = NFS_SB(sb);

	if (!nfs_compare_super_address(old, server))
		return 0;
	/* Note: NFS_MOUNT_UNSHARED == NFS4_MOUNT_UNSHARED */
	if (old->flags & NFS_MOUNT_UNSHARED)
		return 0;
	if (memcmp(&old->fsid, &server->fsid, sizeof(old->fsid)) != 0)
		return 0;
	if (!nfs_compare_userns(old, server))
		return 0;
	if ((old->has_sec_mnt_opts || fc->security) &&
			security_sb_mnt_opts_compat(sb, fc->security))
		return 0;
	return nfs_compare_mount_options(sb, server, fc);
}

#ifdef CONFIG_NFS_FSCACHE
static int nfs_get_cache_cookie(struct super_block *sb,
				struct nfs_fs_context *ctx)
{
	struct nfs_server *nfss = NFS_SB(sb);
	char *uniq = NULL;
	int ulen = 0;

	nfss->fscache = NULL;

	if (!ctx)
		return 0;

	if (ctx->clone_data.sb) {
		struct nfs_server *mnt_s = NFS_SB(ctx->clone_data.sb);
		if (!(mnt_s->options & NFS_OPTION_FSCACHE))
			return 0;
		if (mnt_s->fscache_uniq) {
			uniq = mnt_s->fscache_uniq;
			ulen = strlen(uniq);
		}
	} else {
		if (!(ctx->options & NFS_OPTION_FSCACHE))
			return 0;
		if (ctx->fscache_uniq) {
			uniq = ctx->fscache_uniq;
			ulen = strlen(ctx->fscache_uniq);
		}
	}

	return nfs_fscache_get_super_cookie(sb, uniq, ulen);
}
#else
static int nfs_get_cache_cookie(struct super_block *sb,
				struct nfs_fs_context *ctx)
{
	return 0;
}
#endif

int nfs_get_tree_common(struct fs_context *fc)
{
	struct nfs_fs_context *ctx = nfs_fc2context(fc);
	struct super_block *s;
	int (*compare_super)(struct super_block *, struct fs_context *) = nfs_compare_super;
	struct nfs_server *server = ctx->server;
	int error;

	ctx->server = NULL;
	if (IS_ERR(server))
		return PTR_ERR(server);

	if (server->flags & NFS_MOUNT_UNSHARED)
		compare_super = NULL;

	/* -o noac implies -o sync */
	if (server->flags & NFS_MOUNT_NOAC)
		fc->sb_flags |= SB_SYNCHRONOUS;

	if (ctx->clone_data.sb)
		if (ctx->clone_data.sb->s_flags & SB_SYNCHRONOUS)
			fc->sb_flags |= SB_SYNCHRONOUS;

	/* Get a superblock - note that we may end up sharing one that already exists */
	fc->s_fs_info = server;
	s = sget_fc(fc, compare_super, nfs_set_super);
	fc->s_fs_info = NULL;
	if (IS_ERR(s)) {
		error = PTR_ERR(s);
		nfs_errorf(fc, "NFS: Couldn't get superblock");
		goto out_err_nosb;
	}

	if (s->s_fs_info != server) {
		nfs_free_server(server);
		server = NULL;
	} else {
		error = super_setup_bdi_name(s, "%u:%u", MAJOR(server->s_dev),
					     MINOR(server->s_dev));
		if (error)
			goto error_splat_super;
		s->s_bdi->io_pages = server->rpages;
		server->super = s;
	}

	if (!s->s_root) {
		unsigned bsize = ctx->clone_data.inherited_bsize;
		/* initial superblock/root creation */
		nfs_fill_super(s, ctx);
		if (bsize) {
			s->s_blocksize_bits = bsize;
			s->s_blocksize = 1U << bsize;
		}
		error = nfs_get_cache_cookie(s, ctx);
		if (error < 0)
			goto error_splat_super;
	}

	error = nfs_get_root(s, fc);
	if (error < 0) {
		nfs_errorf(fc, "NFS: Couldn't get root dentry");
		goto error_splat_super;
	}

	s->s_flags |= SB_ACTIVE;
	error = 0;

out:
	return error;

out_err_nosb:
	nfs_free_server(server);
	goto out;
error_splat_super:
	deactivate_locked_super(s);
	goto out;
}

/*
 * Destroy an NFS superblock
 */
void nfs_kill_super(struct super_block *s)
{
	struct nfs_server *server = NFS_SB(s);

	nfs_sysfs_move_sb_to_server(server);
	kill_anon_super(s);

	nfs_fscache_release_super_cookie(s);

	nfs_free_server(server);
}
EXPORT_SYMBOL_GPL(nfs_kill_super);

#if IS_ENABLED(CONFIG_NFS_V4)

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
short nfs_delay_retrans = -1;

EXPORT_SYMBOL_GPL(nfs_callback_nr_threads);
EXPORT_SYMBOL_GPL(nfs_callback_set_tcpport);
EXPORT_SYMBOL_GPL(nfs_idmap_cache_timeout);
EXPORT_SYMBOL_GPL(nfs4_disable_idmapping);
EXPORT_SYMBOL_GPL(max_session_slots);
EXPORT_SYMBOL_GPL(max_session_cb_slots);
EXPORT_SYMBOL_GPL(send_implementation_id);
EXPORT_SYMBOL_GPL(nfs4_client_id_uniquifier);
EXPORT_SYMBOL_GPL(recover_lost_locks);
EXPORT_SYMBOL_GPL(nfs_delay_retrans);

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
#define param_check_portnr(name, p) __param_check(name, p, unsigned int)

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

module_param_named(delay_retrans, nfs_delay_retrans, short, 0644);
MODULE_PARM_DESC(delay_retrans,
		 "Unless negative, specifies the number of times the NFSv4 "
		 "client retries a request before returning an EAGAIN error, "
		 "after a reply of NFS4ERR_DELAY from the server.");
#endif /* CONFIG_NFS_V4 */
