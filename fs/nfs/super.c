/*
 *  linux/fs/nfs/super.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  nfs superblock handling functions
 *
 *  Modularised by Alan Cox <Alan.Cox@linux.org>, while hacking some
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
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/metrics.h>
#include <linux/sunrpc/xprtsock.h>
#include <linux/sunrpc/xprtrdma.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs4_mount.h>
#include <linux/lockd/bind.h>
#include <linux/smp_lock.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/nfs_idmap.h>
#include <linux/vfs.h>
#include <linux/inet.h>
#include <linux/in6.h>
#include <net/ipv6.h>
#include <linux/nfs_xdr.h>
#include <linux/magic.h>
#include <linux/parser.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"
#include "iostat.h"
#include "internal.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

enum {
	/* Mount options that take no arguments */
	Opt_soft, Opt_hard,
	Opt_intr, Opt_nointr,
	Opt_posix, Opt_noposix,
	Opt_cto, Opt_nocto,
	Opt_ac, Opt_noac,
	Opt_lock, Opt_nolock,
	Opt_v2, Opt_v3,
	Opt_udp, Opt_tcp, Opt_rdma,
	Opt_acl, Opt_noacl,
	Opt_rdirplus, Opt_nordirplus,
	Opt_sharecache, Opt_nosharecache,

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
	Opt_nfsvers,

	/* Mount options that take string arguments */
	Opt_sec, Opt_proto, Opt_mountproto, Opt_mounthost,
	Opt_addr, Opt_mountaddr, Opt_clientaddr,

	/* Mount options that are ignored */
	Opt_userspace, Opt_deprecated,

	Opt_err
};

static match_table_t nfs_mount_option_tokens = {
	{ Opt_userspace, "bg" },
	{ Opt_userspace, "fg" },
	{ Opt_soft, "soft" },
	{ Opt_hard, "hard" },
	{ Opt_intr, "intr" },
	{ Opt_nointr, "nointr" },
	{ Opt_posix, "posix" },
	{ Opt_noposix, "noposix" },
	{ Opt_cto, "cto" },
	{ Opt_nocto, "nocto" },
	{ Opt_ac, "ac" },
	{ Opt_noac, "noac" },
	{ Opt_lock, "lock" },
	{ Opt_nolock, "nolock" },
	{ Opt_v2, "v2" },
	{ Opt_v3, "v3" },
	{ Opt_udp, "udp" },
	{ Opt_tcp, "tcp" },
	{ Opt_rdma, "rdma" },
	{ Opt_acl, "acl" },
	{ Opt_noacl, "noacl" },
	{ Opt_rdirplus, "rdirplus" },
	{ Opt_nordirplus, "nordirplus" },
	{ Opt_sharecache, "sharecache" },
	{ Opt_nosharecache, "nosharecache" },

	{ Opt_port, "port=%u" },
	{ Opt_rsize, "rsize=%u" },
	{ Opt_wsize, "wsize=%u" },
	{ Opt_bsize, "bsize=%u" },
	{ Opt_timeo, "timeo=%u" },
	{ Opt_retrans, "retrans=%u" },
	{ Opt_acregmin, "acregmin=%u" },
	{ Opt_acregmax, "acregmax=%u" },
	{ Opt_acdirmin, "acdirmin=%u" },
	{ Opt_acdirmax, "acdirmax=%u" },
	{ Opt_actimeo, "actimeo=%u" },
	{ Opt_userspace, "retry=%u" },
	{ Opt_namelen, "namlen=%u" },
	{ Opt_mountport, "mountport=%u" },
	{ Opt_mountvers, "mountvers=%u" },
	{ Opt_nfsvers, "nfsvers=%u" },
	{ Opt_nfsvers, "vers=%u" },

	{ Opt_sec, "sec=%s" },
	{ Opt_proto, "proto=%s" },
	{ Opt_mountproto, "mountproto=%s" },
	{ Opt_addr, "addr=%s" },
	{ Opt_clientaddr, "clientaddr=%s" },
	{ Opt_mounthost, "mounthost=%s" },
	{ Opt_mountaddr, "mountaddr=%s" },

	{ Opt_err, NULL }
};

enum {
	Opt_xprt_udp, Opt_xprt_tcp, Opt_xprt_rdma,

	Opt_xprt_err
};

static match_table_t nfs_xprt_protocol_tokens = {
	{ Opt_xprt_udp, "udp" },
	{ Opt_xprt_tcp, "tcp" },
	{ Opt_xprt_rdma, "rdma" },

	{ Opt_xprt_err, NULL }
};

enum {
	Opt_sec_none, Opt_sec_sys,
	Opt_sec_krb5, Opt_sec_krb5i, Opt_sec_krb5p,
	Opt_sec_lkey, Opt_sec_lkeyi, Opt_sec_lkeyp,
	Opt_sec_spkm, Opt_sec_spkmi, Opt_sec_spkmp,

	Opt_sec_err
};

static match_table_t nfs_secflavor_tokens = {
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


static void nfs_umount_begin(struct vfsmount *, int);
static int  nfs_statfs(struct dentry *, struct kstatfs *);
static int  nfs_show_options(struct seq_file *, struct vfsmount *);
static int  nfs_show_stats(struct seq_file *, struct vfsmount *);
static int nfs_get_sb(struct file_system_type *, int, const char *, void *, struct vfsmount *);
static int nfs_xdev_get_sb(struct file_system_type *fs_type,
		int flags, const char *dev_name, void *raw_data, struct vfsmount *mnt);
static void nfs_kill_super(struct super_block *);
static void nfs_put_super(struct super_block *);

static struct file_system_type nfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs",
	.get_sb		= nfs_get_sb,
	.kill_sb	= nfs_kill_super,
	.fs_flags	= FS_RENAME_DOES_D_MOVE|FS_REVAL_DOT|FS_BINARY_MOUNTDATA,
};

struct file_system_type nfs_xdev_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs",
	.get_sb		= nfs_xdev_get_sb,
	.kill_sb	= nfs_kill_super,
	.fs_flags	= FS_RENAME_DOES_D_MOVE|FS_REVAL_DOT|FS_BINARY_MOUNTDATA,
};

static const struct super_operations nfs_sops = {
	.alloc_inode	= nfs_alloc_inode,
	.destroy_inode	= nfs_destroy_inode,
	.write_inode	= nfs_write_inode,
	.put_super	= nfs_put_super,
	.statfs		= nfs_statfs,
	.clear_inode	= nfs_clear_inode,
	.umount_begin	= nfs_umount_begin,
	.show_options	= nfs_show_options,
	.show_stats	= nfs_show_stats,
};

#ifdef CONFIG_NFS_V4
static int nfs4_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data, struct vfsmount *mnt);
static int nfs4_xdev_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data, struct vfsmount *mnt);
static int nfs4_referral_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data, struct vfsmount *mnt);
static void nfs4_kill_super(struct super_block *sb);

static struct file_system_type nfs4_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs4",
	.get_sb		= nfs4_get_sb,
	.kill_sb	= nfs4_kill_super,
	.fs_flags	= FS_RENAME_DOES_D_MOVE|FS_REVAL_DOT|FS_BINARY_MOUNTDATA,
};

struct file_system_type nfs4_xdev_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs4",
	.get_sb		= nfs4_xdev_get_sb,
	.kill_sb	= nfs4_kill_super,
	.fs_flags	= FS_RENAME_DOES_D_MOVE|FS_REVAL_DOT|FS_BINARY_MOUNTDATA,
};

struct file_system_type nfs4_referral_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs4",
	.get_sb		= nfs4_referral_get_sb,
	.kill_sb	= nfs4_kill_super,
	.fs_flags	= FS_RENAME_DOES_D_MOVE|FS_REVAL_DOT|FS_BINARY_MOUNTDATA,
};

static const struct super_operations nfs4_sops = {
	.alloc_inode	= nfs_alloc_inode,
	.destroy_inode	= nfs_destroy_inode,
	.write_inode	= nfs_write_inode,
	.statfs		= nfs_statfs,
	.clear_inode	= nfs4_clear_inode,
	.umount_begin	= nfs_umount_begin,
	.show_options	= nfs_show_options,
	.show_stats	= nfs_show_stats,
};
#endif

static struct shrinker acl_shrinker = {
	.shrink		= nfs_access_cache_shrinker,
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

	ret = nfs_register_sysctl();
	if (ret < 0)
		goto error_1;
#ifdef CONFIG_NFS_V4
	ret = register_filesystem(&nfs4_fs_type);
	if (ret < 0)
		goto error_2;
#endif
	register_shrinker(&acl_shrinker);
	return 0;

#ifdef CONFIG_NFS_V4
error_2:
	nfs_unregister_sysctl();
#endif
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
#ifdef CONFIG_NFS_V4
	unregister_filesystem(&nfs4_fs_type);
#endif
	nfs_unregister_sysctl();
	unregister_filesystem(&nfs_fs_type);
}

void nfs_sb_active(struct nfs_server *server)
{
	atomic_inc(&server->active);
}

void nfs_sb_deactive(struct nfs_server *server)
{
	if (atomic_dec_and_test(&server->active))
		wake_up(&server->active_wq);
}

static void nfs_put_super(struct super_block *sb)
{
	struct nfs_server *server = NFS_SB(sb);
	/*
	 * Make sure there are no outstanding ops to this server.
	 * If so, wait for them to finish before allowing the
	 * unmount to continue.
	 */
	wait_event(server->active_wq, atomic_read(&server->active) == 0);
}

/*
 * Deliver file system statistics to userspace
 */
static int nfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct nfs_server *server = NFS_SB(dentry->d_sb);
	unsigned char blockbits;
	unsigned long blockres;
	struct nfs_fh *fh = NFS_FH(dentry->d_inode);
	struct nfs_fattr fattr;
	struct nfs_fsstat res = {
			.fattr = &fattr,
	};
	int error;

	lock_kernel();

	error = server->nfs_client->rpc_ops->statfs(server, fh, &res);
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

	unlock_kernel();
	return 0;

 out_err:
	dprintk("%s: statfs error = %d\n", __FUNCTION__, -error);
	unlock_kernel();
	return error;
}

/*
 * Map the security flavour number to a name
 */
static const char *nfs_pseudoflavour_to_name(rpc_authflavor_t flavour)
{
	static const struct {
		rpc_authflavor_t flavour;
		const char *str;
	} sec_flavours[] = {
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

static void nfs_show_mountd_options(struct seq_file *m, struct nfs_server *nfss,
				    int showdefaults)
{
	struct sockaddr *sap = (struct sockaddr *)&nfss->mountd_address;

	switch (sap->sa_family) {
	case AF_INET: {
		struct sockaddr_in *sin = (struct sockaddr_in *)sap;
		seq_printf(m, ",mountaddr=" NIPQUAD_FMT,
				NIPQUAD(sin->sin_addr.s_addr));
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sap;
		seq_printf(m, ",mountaddr=" NIP6_FMT,
				NIP6(sin6->sin6_addr));
		break;
	}
	default:
		if (showdefaults)
			seq_printf(m, ",mountaddr=unspecified");
	}

	if (nfss->mountd_version || showdefaults)
		seq_printf(m, ",mountvers=%u", nfss->mountd_version);
	if (nfss->mountd_port || showdefaults)
		seq_printf(m, ",mountport=%u", nfss->mountd_port);

	switch (nfss->mountd_protocol) {
	case IPPROTO_UDP:
		seq_printf(m, ",mountproto=udp");
		break;
	case IPPROTO_TCP:
		seq_printf(m, ",mountproto=tcp");
		break;
	default:
		if (showdefaults)
			seq_printf(m, ",mountproto=auto");
	}
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
		{ NFS_MOUNT_INTR, ",intr", ",nointr" },
		{ NFS_MOUNT_POSIX, ",posix", "" },
		{ NFS_MOUNT_NOCTO, ",nocto", "" },
		{ NFS_MOUNT_NOAC, ",noac", "" },
		{ NFS_MOUNT_NONLM, ",nolock", "" },
		{ NFS_MOUNT_NOACL, ",noacl", "" },
		{ NFS_MOUNT_NORDIRPLUS, ",nordirplus", "" },
		{ NFS_MOUNT_UNSHARED, ",nosharecache", ""},
		{ 0, NULL, NULL }
	};
	const struct proc_nfs_info *nfs_infop;
	struct nfs_client *clp = nfss->nfs_client;
	u32 version = clp->rpc_ops->version;

	seq_printf(m, ",vers=%u", version);
	seq_printf(m, ",rsize=%u", nfss->rsize);
	seq_printf(m, ",wsize=%u", nfss->wsize);
	if (nfss->bsize != 0)
		seq_printf(m, ",bsize=%u", nfss->bsize);
	seq_printf(m, ",namlen=%u", nfss->namelen);
	if (nfss->acregmin != 3*HZ || showdefaults)
		seq_printf(m, ",acregmin=%u", nfss->acregmin/HZ);
	if (nfss->acregmax != 60*HZ || showdefaults)
		seq_printf(m, ",acregmax=%u", nfss->acregmax/HZ);
	if (nfss->acdirmin != 30*HZ || showdefaults)
		seq_printf(m, ",acdirmin=%u", nfss->acdirmin/HZ);
	if (nfss->acdirmax != 60*HZ || showdefaults)
		seq_printf(m, ",acdirmax=%u", nfss->acdirmax/HZ);
	for (nfs_infop = nfs_info; nfs_infop->flag; nfs_infop++) {
		if (nfss->flags & nfs_infop->flag)
			seq_puts(m, nfs_infop->str);
		else
			seq_puts(m, nfs_infop->nostr);
	}
	seq_printf(m, ",proto=%s",
		   rpc_peeraddr2str(nfss->client, RPC_DISPLAY_PROTO));
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

#ifdef CONFIG_NFS_V4
	if (clp->rpc_ops->version == 4)
		seq_printf(m, ",clientaddr=%s", clp->cl_ipaddr);
#endif
}

/*
 * Describe the mount options on this VFS mountpoint
 */
static int nfs_show_options(struct seq_file *m, struct vfsmount *mnt)
{
	struct nfs_server *nfss = NFS_SB(mnt->mnt_sb);

	nfs_show_mount_options(m, nfss, 0);

	seq_printf(m, ",addr=%s",
			rpc_peeraddr2str(nfss->nfs_client->cl_rpcclient,
							RPC_DISPLAY_ADDR));

	return 0;
}

/*
 * Present statistical information for this VFS mountpoint
 */
static int nfs_show_stats(struct seq_file *m, struct vfsmount *mnt)
{
	int i, cpu;
	struct nfs_server *nfss = NFS_SB(mnt->mnt_sb);
	struct rpc_auth *auth = nfss->client->cl_auth;
	struct nfs_iostats totals = { };

	seq_printf(m, "statvers=%s", NFS_IOSTAT_VERS);

	/*
	 * Display all mount option settings
	 */
	seq_printf(m, "\n\topts:\t");
	seq_puts(m, mnt->mnt_sb->s_flags & MS_RDONLY ? "ro" : "rw");
	seq_puts(m, mnt->mnt_sb->s_flags & MS_SYNCHRONOUS ? ",sync" : "");
	seq_puts(m, mnt->mnt_sb->s_flags & MS_NOATIME ? ",noatime" : "");
	seq_puts(m, mnt->mnt_sb->s_flags & MS_NODIRATIME ? ",nodiratime" : "");
	nfs_show_mount_options(m, nfss, 1);

	seq_printf(m, "\n\tage:\t%lu", (jiffies - nfss->mount_time) / HZ);

	seq_printf(m, "\n\tcaps:\t");
	seq_printf(m, "caps=0x%x", nfss->caps);
	seq_printf(m, ",wtmult=%u", nfss->wtmult);
	seq_printf(m, ",dtsize=%u", nfss->dtsize);
	seq_printf(m, ",bsize=%u", nfss->bsize);
	seq_printf(m, ",namlen=%u", nfss->namelen);

#ifdef CONFIG_NFS_V4
	if (nfss->nfs_client->rpc_ops->version == 4) {
		seq_printf(m, "\n\tnfsv4:\t");
		seq_printf(m, "bm0=0x%x", nfss->attr_bitmask[0]);
		seq_printf(m, ",bm1=0x%x", nfss->attr_bitmask[1]);
		seq_printf(m, ",acl=0x%x", nfss->acl_bitmask);
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

	seq_printf(m, "\n\tevents:\t");
	for (i = 0; i < __NFSIOS_COUNTSMAX; i++)
		seq_printf(m, "%lu ", totals.events[i]);
	seq_printf(m, "\n\tbytes:\t");
	for (i = 0; i < __NFSIOS_BYTESMAX; i++)
		seq_printf(m, "%Lu ", totals.bytes[i]);
	seq_printf(m, "\n");

	rpc_print_iostats(m, nfss->client);

	return 0;
}

/*
 * Begin unmount by attempting to remove all automounted mountpoints we added
 * in response to xdev traversals and referrals
 */
static void nfs_umount_begin(struct vfsmount *vfsmnt, int flags)
{
	struct nfs_server *server = NFS_SB(vfsmnt->mnt_sb);
	struct rpc_clnt *rpc;

	if (!(flags & MNT_FORCE))
		return;
	/* -EIO all pending I/O */
	rpc = server->client_acl;
	if (!IS_ERR(rpc))
		rpc_killall_tasks(rpc);
	rpc = server->client;
	if (!IS_ERR(rpc))
		rpc_killall_tasks(rpc);
}

/*
 * Set the port number in an address.  Be agnostic about the address family.
 */
static void nfs_set_port(struct sockaddr *sap, unsigned short port)
{
	switch (sap->sa_family) {
	case AF_INET: {
		struct sockaddr_in *ap = (struct sockaddr_in *)sap;
		ap->sin_port = htons(port);
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 *ap = (struct sockaddr_in6 *)sap;
		ap->sin6_port = htons(port);
		break;
	}
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

	return 0;
}

/*
 * Parse string addresses passed in via a mount option,
 * and construct a sockaddr based on the result.
 *
 * If address parsing fails, set the sockaddr's address
 * family to AF_UNSPEC to force nfs_verify_server_address()
 * to punt the mount.
 */
static void nfs_parse_server_address(char *value,
				     struct sockaddr *sap,
				     size_t *len)
{
	if (strchr(value, ':')) {
		struct sockaddr_in6 *ap = (struct sockaddr_in6 *)sap;
		u8 *addr = (u8 *)&ap->sin6_addr.in6_u;

		ap->sin6_family = AF_INET6;
		*len = sizeof(*ap);
		if (in6_pton(value, -1, addr, '\0', NULL))
			return;
	} else {
		struct sockaddr_in *ap = (struct sockaddr_in *)sap;
		u8 *addr = (u8 *)&ap->sin_addr.s_addr;

		ap->sin_family = AF_INET;
		*len = sizeof(*ap);
		if (in4_pton(value, -1, addr, '\0', NULL))
			return;
	}

	sap->sa_family = AF_UNSPEC;
	*len = 0;
}

/*
 * Error-check and convert a string of mount options from user space into
 * a data structure
 */
static int nfs_parse_mount_options(char *raw,
				   struct nfs_parsed_mount_data *mnt)
{
	char *p, *string, *secdata;
	int rc;

	if (!raw) {
		dfprintk(MOUNT, "NFS: mount options string was NULL.\n");
		return 1;
	}
	dfprintk(MOUNT, "NFS: nfs mount opts='%s'\n", raw);

	secdata = alloc_secdata();
	if (!secdata)
		goto out_nomem;

	rc = security_sb_copy_data(raw, secdata);
	if (rc)
		goto out_security_failure;

	rc = security_sb_parse_opts_str(secdata, &mnt->lsm_opts);
	if (rc)
		goto out_security_failure;

	free_secdata(secdata);

	while ((p = strsep(&raw, ",")) != NULL) {
		substring_t args[MAX_OPT_ARGS];
		int option, token;

		if (!*p)
			continue;

		dfprintk(MOUNT, "NFS:   parsing nfs mount option '%s'\n", p);

		token = match_token(p, nfs_mount_option_tokens, args);
		switch (token) {
		case Opt_soft:
			mnt->flags |= NFS_MOUNT_SOFT;
			break;
		case Opt_hard:
			mnt->flags &= ~NFS_MOUNT_SOFT;
			break;
		case Opt_intr:
		case Opt_nointr:
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
			break;
		case Opt_nolock:
			mnt->flags |= NFS_MOUNT_NONLM;
			break;
		case Opt_v2:
			mnt->flags &= ~NFS_MOUNT_VER3;
			break;
		case Opt_v3:
			mnt->flags |= NFS_MOUNT_VER3;
			break;
		case Opt_udp:
			mnt->flags &= ~NFS_MOUNT_TCP;
			mnt->nfs_server.protocol = XPRT_TRANSPORT_UDP;
			mnt->timeo = 7;
			mnt->retrans = 5;
			break;
		case Opt_tcp:
			mnt->flags |= NFS_MOUNT_TCP;
			mnt->nfs_server.protocol = XPRT_TRANSPORT_TCP;
			mnt->timeo = 600;
			mnt->retrans = 2;
			break;
		case Opt_rdma:
			mnt->flags |= NFS_MOUNT_TCP; /* for side protocols */
			mnt->nfs_server.protocol = XPRT_TRANSPORT_RDMA;
			mnt->timeo = 600;
			mnt->retrans = 2;
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

		case Opt_port:
			if (match_int(args, &option))
				return 0;
			if (option < 0 || option > 65535)
				return 0;
			mnt->nfs_server.port = option;
			break;
		case Opt_rsize:
			if (match_int(args, &mnt->rsize))
				return 0;
			break;
		case Opt_wsize:
			if (match_int(args, &mnt->wsize))
				return 0;
			break;
		case Opt_bsize:
			if (match_int(args, &option))
				return 0;
			if (option < 0)
				return 0;
			mnt->bsize = option;
			break;
		case Opt_timeo:
			if (match_int(args, &mnt->timeo))
				return 0;
			break;
		case Opt_retrans:
			if (match_int(args, &mnt->retrans))
				return 0;
			break;
		case Opt_acregmin:
			if (match_int(args, &mnt->acregmin))
				return 0;
			break;
		case Opt_acregmax:
			if (match_int(args, &mnt->acregmax))
				return 0;
			break;
		case Opt_acdirmin:
			if (match_int(args, &mnt->acdirmin))
				return 0;
			break;
		case Opt_acdirmax:
			if (match_int(args, &mnt->acdirmax))
				return 0;
			break;
		case Opt_actimeo:
			if (match_int(args, &option))
				return 0;
			if (option < 0)
				return 0;
			mnt->acregmin =
			mnt->acregmax =
			mnt->acdirmin =
			mnt->acdirmax = option;
			break;
		case Opt_namelen:
			if (match_int(args, &mnt->namlen))
				return 0;
			break;
		case Opt_mountport:
			if (match_int(args, &option))
				return 0;
			if (option < 0 || option > 65535)
				return 0;
			mnt->mount_server.port = option;
			break;
		case Opt_mountvers:
			if (match_int(args, &option))
				return 0;
			if (option < 0)
				return 0;
			mnt->mount_server.version = option;
			break;
		case Opt_nfsvers:
			if (match_int(args, &option))
				return 0;
			switch (option) {
			case 2:
				mnt->flags &= ~NFS_MOUNT_VER3;
				break;
			case 3:
				mnt->flags |= NFS_MOUNT_VER3;
				break;
			default:
				goto out_unrec_vers;
			}
			break;

		case Opt_sec:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			token = match_token(string, nfs_secflavor_tokens, args);
			kfree(string);

			/*
			 * The flags setting is for v2/v3.  The flavor_len
			 * setting is for v4.  v2/v3 also need to know the
			 * difference between NULL and UNIX.
			 */
			switch (token) {
			case Opt_sec_none:
				mnt->flags &= ~NFS_MOUNT_SECFLAVOUR;
				mnt->auth_flavor_len = 0;
				mnt->auth_flavors[0] = RPC_AUTH_NULL;
				break;
			case Opt_sec_sys:
				mnt->flags &= ~NFS_MOUNT_SECFLAVOUR;
				mnt->auth_flavor_len = 0;
				mnt->auth_flavors[0] = RPC_AUTH_UNIX;
				break;
			case Opt_sec_krb5:
				mnt->flags |= NFS_MOUNT_SECFLAVOUR;
				mnt->auth_flavor_len = 1;
				mnt->auth_flavors[0] = RPC_AUTH_GSS_KRB5;
				break;
			case Opt_sec_krb5i:
				mnt->flags |= NFS_MOUNT_SECFLAVOUR;
				mnt->auth_flavor_len = 1;
				mnt->auth_flavors[0] = RPC_AUTH_GSS_KRB5I;
				break;
			case Opt_sec_krb5p:
				mnt->flags |= NFS_MOUNT_SECFLAVOUR;
				mnt->auth_flavor_len = 1;
				mnt->auth_flavors[0] = RPC_AUTH_GSS_KRB5P;
				break;
			case Opt_sec_lkey:
				mnt->flags |= NFS_MOUNT_SECFLAVOUR;
				mnt->auth_flavor_len = 1;
				mnt->auth_flavors[0] = RPC_AUTH_GSS_LKEY;
				break;
			case Opt_sec_lkeyi:
				mnt->flags |= NFS_MOUNT_SECFLAVOUR;
				mnt->auth_flavor_len = 1;
				mnt->auth_flavors[0] = RPC_AUTH_GSS_LKEYI;
				break;
			case Opt_sec_lkeyp:
				mnt->flags |= NFS_MOUNT_SECFLAVOUR;
				mnt->auth_flavor_len = 1;
				mnt->auth_flavors[0] = RPC_AUTH_GSS_LKEYP;
				break;
			case Opt_sec_spkm:
				mnt->flags |= NFS_MOUNT_SECFLAVOUR;
				mnt->auth_flavor_len = 1;
				mnt->auth_flavors[0] = RPC_AUTH_GSS_SPKM;
				break;
			case Opt_sec_spkmi:
				mnt->flags |= NFS_MOUNT_SECFLAVOUR;
				mnt->auth_flavor_len = 1;
				mnt->auth_flavors[0] = RPC_AUTH_GSS_SPKMI;
				break;
			case Opt_sec_spkmp:
				mnt->flags |= NFS_MOUNT_SECFLAVOUR;
				mnt->auth_flavor_len = 1;
				mnt->auth_flavors[0] = RPC_AUTH_GSS_SPKMP;
				break;
			default:
				goto out_unrec_sec;
			}
			break;
		case Opt_proto:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			token = match_token(string,
					    nfs_xprt_protocol_tokens, args);
			kfree(string);

			switch (token) {
			case Opt_xprt_udp:
				mnt->flags &= ~NFS_MOUNT_TCP;
				mnt->nfs_server.protocol = XPRT_TRANSPORT_UDP;
				mnt->timeo = 7;
				mnt->retrans = 5;
				break;
			case Opt_xprt_tcp:
				mnt->flags |= NFS_MOUNT_TCP;
				mnt->nfs_server.protocol = XPRT_TRANSPORT_TCP;
				mnt->timeo = 600;
				mnt->retrans = 2;
				break;
			case Opt_xprt_rdma:
				/* vector side protocols to TCP */
				mnt->flags |= NFS_MOUNT_TCP;
				mnt->nfs_server.protocol = XPRT_TRANSPORT_RDMA;
				mnt->timeo = 600;
				mnt->retrans = 2;
				break;
			default:
				goto out_unrec_xprt;
			}
			break;
		case Opt_mountproto:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			token = match_token(string,
					    nfs_xprt_protocol_tokens, args);
			kfree(string);

			switch (token) {
			case Opt_xprt_udp:
				mnt->mount_server.protocol = XPRT_TRANSPORT_UDP;
				break;
			case Opt_xprt_tcp:
				mnt->mount_server.protocol = XPRT_TRANSPORT_TCP;
				break;
			case Opt_xprt_rdma: /* not used for side protocols */
			default:
				goto out_unrec_xprt;
			}
			break;
		case Opt_addr:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			nfs_parse_server_address(string, (struct sockaddr *)
						 &mnt->nfs_server.address,
						 &mnt->nfs_server.addrlen);
			kfree(string);
			break;
		case Opt_clientaddr:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			kfree(mnt->client_address);
			mnt->client_address = string;
			break;
		case Opt_mounthost:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			kfree(mnt->mount_server.hostname);
			mnt->mount_server.hostname = string;
			break;
		case Opt_mountaddr:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			nfs_parse_server_address(string, (struct sockaddr *)
						 &mnt->mount_server.address,
						 &mnt->mount_server.addrlen);
			kfree(string);
			break;

		case Opt_userspace:
		case Opt_deprecated:
			break;

		default:
			goto out_unknown;
		}
	}

	nfs_set_port((struct sockaddr *)&mnt->nfs_server.address,
				mnt->nfs_server.port);

	return 1;

out_nomem:
	printk(KERN_INFO "NFS: not enough memory to parse option\n");
	return 0;
out_security_failure:
	free_secdata(secdata);
	printk(KERN_INFO "NFS: security options invalid: %d\n", rc);
	return 0;
out_unrec_vers:
	printk(KERN_INFO "NFS: unrecognized NFS version number\n");
	return 0;

out_unrec_xprt:
	printk(KERN_INFO "NFS: unrecognized transport protocol\n");
	return 0;

out_unrec_sec:
	printk(KERN_INFO "NFS: unrecognized security flavor\n");
	return 0;

out_unknown:
	printk(KERN_INFO "NFS: unknown mount option: %s\n", p);
	return 0;
}

/*
 * Use the remote server's MOUNT service to request the NFS file handle
 * corresponding to the provided path.
 */
static int nfs_try_mount(struct nfs_parsed_mount_data *args,
			 struct nfs_fh *root_fh)
{
	struct sockaddr *sap = (struct sockaddr *)&args->mount_server.address;
	char *hostname;
	int status;

	if (args->mount_server.version == 0) {
		if (args->flags & NFS_MOUNT_VER3)
			args->mount_server.version = NFS_MNT3_VERSION;
		else
			args->mount_server.version = NFS_MNT_VERSION;
	}

	if (args->mount_server.hostname)
		hostname = args->mount_server.hostname;
	else
		hostname = args->nfs_server.hostname;

	/*
	 * Construct the mount server's address.
	 */
	if (args->mount_server.address.ss_family == AF_UNSPEC) {
		memcpy(sap, &args->nfs_server.address,
		       args->nfs_server.addrlen);
		args->mount_server.addrlen = args->nfs_server.addrlen;
	}

	/*
	 * autobind will be used if mount_server.port == 0
	 */
	nfs_set_port(sap, args->mount_server.port);

	/*
	 * Now ask the mount server to map our export path
	 * to a file handle.
	 */
	status = nfs_mount(sap,
			   args->mount_server.addrlen,
			   hostname,
			   args->nfs_server.export_path,
			   args->mount_server.version,
			   args->mount_server.protocol,
			   root_fh);
	if (status == 0)
		return 0;

	dfprintk(MOUNT, "NFS: unable to mount server %s, error %d",
			hostname, status);
	return status;
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
static int nfs_validate_mount_data(void *options,
				   struct nfs_parsed_mount_data *args,
				   struct nfs_fh *mntfh,
				   const char *dev_name)
{
	struct nfs_mount_data *data = (struct nfs_mount_data *)options;

	memset(args, 0, sizeof(*args));

	if (data == NULL)
		goto out_no_data;

	args->flags		= (NFS_MOUNT_VER3 | NFS_MOUNT_TCP);
	args->rsize		= NFS_MAX_FILE_IO_SIZE;
	args->wsize		= NFS_MAX_FILE_IO_SIZE;
	args->timeo		= 600;
	args->retrans		= 2;
	args->acregmin		= 3;
	args->acregmax		= 60;
	args->acdirmin		= 30;
	args->acdirmax		= 60;
	args->mount_server.port	= 0;	/* autobind unless user sets port */
	args->mount_server.protocol = XPRT_TRANSPORT_UDP;
	args->nfs_server.port	= 0;	/* autobind unless user sets port */
	args->nfs_server.protocol = XPRT_TRANSPORT_TCP;

	switch (data->version) {
	case 1:
		data->namlen = 0;
	case 2:
		data->bsize = 0;
	case 3:
		if (data->flags & NFS_MOUNT_VER3)
			goto out_no_v3;
		data->root.size = NFS2_FHSIZE;
		memcpy(data->root.data, data->old_root.data, NFS2_FHSIZE);
	case 4:
		if (data->flags & NFS_MOUNT_SECFLAVOUR)
			goto out_no_sec;
	case 5:
		memset(data->context, 0, sizeof(data->context));
	case 6:
		if (data->flags & NFS_MOUNT_VER3)
			mntfh->size = data->root.size;
		else
			mntfh->size = NFS2_FHSIZE;

		if (mntfh->size > sizeof(mntfh->data))
			goto out_invalid_fh;

		memcpy(mntfh->data, data->root.data, mntfh->size);
		if (mntfh->size < sizeof(mntfh->data))
			memset(mntfh->data + mntfh->size, 0,
			       sizeof(mntfh->data) - mntfh->size);

		/*
		 * Translate to nfs_parsed_mount_data, which nfs_fill_super
		 * can deal with.
		 */
		args->flags		= data->flags;
		args->rsize		= data->rsize;
		args->wsize		= data->wsize;
		args->timeo		= data->timeo;
		args->retrans		= data->retrans;
		args->acregmin		= data->acregmin;
		args->acregmax		= data->acregmax;
		args->acdirmin		= data->acdirmin;
		args->acdirmax		= data->acdirmax;

		memcpy(&args->nfs_server.address, &data->addr,
		       sizeof(data->addr));
		args->nfs_server.addrlen = sizeof(data->addr);
		if (!nfs_verify_server_address((struct sockaddr *)
						&args->nfs_server.address))
			goto out_no_address;

		if (!(data->flags & NFS_MOUNT_TCP))
			args->nfs_server.protocol = XPRT_TRANSPORT_UDP;
		/* N.B. caller will free nfs_server.hostname in all cases */
		args->nfs_server.hostname = kstrdup(data->hostname, GFP_KERNEL);
		args->namlen		= data->namlen;
		args->bsize		= data->bsize;
		args->auth_flavors[0]	= data->pseudoflavor;
		if (!args->nfs_server.hostname)
			goto out_nomem;

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
			char *opts_str = kmalloc(sizeof(data->context) + 8, GFP_KERNEL);
			if (!opts_str)
				return -ENOMEM;
			strcpy(opts_str, "context=");
			data->context[NFS_MAX_CONTEXT_LEN] = '\0';
			strcat(opts_str, &data->context[0]);
			rc = security_sb_parse_opts_str(opts_str, &args->lsm_opts);
			kfree(opts_str);
			if (rc)
				return rc;
#else
			return -EINVAL;
#endif
		}

		break;
	default: {
		unsigned int len;
		char *c;
		int status;

		if (nfs_parse_mount_options((char *)options, args) == 0)
			return -EINVAL;

		if (!nfs_verify_server_address((struct sockaddr *)
						&args->nfs_server.address))
			goto out_no_address;

		c = strchr(dev_name, ':');
		if (c == NULL)
			return -EINVAL;
		len = c - dev_name;
		/* N.B. caller will free nfs_server.hostname in all cases */
		args->nfs_server.hostname = kstrndup(dev_name, len, GFP_KERNEL);
		if (!args->nfs_server.hostname)
			goto out_nomem;

		c++;
		if (strlen(c) > NFS_MAXPATHLEN)
			return -ENAMETOOLONG;
		args->nfs_server.export_path = c;

		status = nfs_try_mount(args, mntfh);
		if (status)
			return status;

		break;
		}
	}

	if (!(args->flags & NFS_MOUNT_SECFLAVOUR))
		args->auth_flavors[0] = RPC_AUTH_UNIX;

#ifndef CONFIG_NFS_V3
	if (args->flags & NFS_MOUNT_VER3)
		goto out_v3_not_compiled;
#endif /* !CONFIG_NFS_V3 */

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

#ifndef CONFIG_NFS_V3
out_v3_not_compiled:
	dfprintk(MOUNT, "NFS: NFSv3 is not compiled into kernel\n");
	return -EPROTONOSUPPORT;
#endif /* !CONFIG_NFS_V3 */

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

/*
 * Initialise the common bits of the superblock
 */
static inline void nfs_initialise_sb(struct super_block *sb)
{
	struct nfs_server *server = NFS_SB(sb);

	sb->s_magic = NFS_SUPER_MAGIC;

	/* We probably want something more informative here */
	snprintf(sb->s_id, sizeof(sb->s_id),
		 "%x:%x", MAJOR(sb->s_dev), MINOR(sb->s_dev));

	if (sb->s_blocksize == 0)
		sb->s_blocksize = nfs_block_bits(server->wsize,
						 &sb->s_blocksize_bits);

	if (server->flags & NFS_MOUNT_NOAC)
		sb->s_flags |= MS_SYNCHRONOUS;

	nfs_super_set_maxbytes(sb, server->maxfilesize);
}

/*
 * Finish setting up an NFS2/3 superblock
 */
static void nfs_fill_super(struct super_block *sb,
			   struct nfs_parsed_mount_data *data)
{
	struct nfs_server *server = NFS_SB(sb);

	sb->s_blocksize_bits = 0;
	sb->s_blocksize = 0;
	if (data->bsize)
		sb->s_blocksize = nfs_block_size(data->bsize, &sb->s_blocksize_bits);

	if (server->flags & NFS_MOUNT_VER3) {
		/* The VFS shouldn't apply the umask to mode bits. We will do
		 * so ourselves when necessary.
		 */
		sb->s_flags |= MS_POSIXACL;
		sb->s_time_gran = 1;
	}

	sb->s_op = &nfs_sops;
 	nfs_initialise_sb(sb);
}

/*
 * Finish setting up a cloned NFS2/3 superblock
 */
static void nfs_clone_super(struct super_block *sb,
			    const struct super_block *old_sb)
{
	struct nfs_server *server = NFS_SB(sb);

	sb->s_blocksize_bits = old_sb->s_blocksize_bits;
	sb->s_blocksize = old_sb->s_blocksize;
	sb->s_maxbytes = old_sb->s_maxbytes;

	if (server->flags & NFS_MOUNT_VER3) {
		/* The VFS shouldn't apply the umask to mode bits. We will do
		 * so ourselves when necessary.
		 */
		sb->s_flags |= MS_POSIXACL;
		sb->s_time_gran = 1;
	}

	sb->s_op = old_sb->s_op;
 	nfs_initialise_sb(sb);
}

#define NFS_MS_MASK (MS_RDONLY|MS_NOSUID|MS_NODEV|MS_NOEXEC|MS_SYNCHRONOUS)

static int nfs_compare_mount_options(const struct super_block *s, const struct nfs_server *b, int flags)
{
	const struct nfs_server *a = s->s_fs_info;
	const struct rpc_clnt *clnt_a = a->client;
	const struct rpc_clnt *clnt_b = b->client;

	if ((s->s_flags & NFS_MS_MASK) != (flags & NFS_MS_MASK))
		goto Ebusy;
	if (a->nfs_client != b->nfs_client)
		goto Ebusy;
	if (a->flags != b->flags)
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
	ret = set_anon_super(s, server);
	if (ret == 0)
		server->s_dev = s->s_dev;
	return ret;
}

static int nfs_compare_super_address(struct nfs_server *server1,
				     struct nfs_server *server2)
{
	struct sockaddr *sap1, *sap2;

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

static int nfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data, struct vfsmount *mnt)
{
	struct nfs_server *server = NULL;
	struct super_block *s;
	struct nfs_fh mntfh;
	struct nfs_parsed_mount_data data;
	struct dentry *mntroot;
	int (*compare_super)(struct super_block *, void *) = nfs_compare_super;
	struct nfs_sb_mountdata sb_mntdata = {
		.mntflags = flags,
	};
	int error;

	security_init_mnt_opts(&data.lsm_opts);

	/* Validate the mount data */
	error = nfs_validate_mount_data(raw_data, &data, &mntfh, dev_name);
	if (error < 0)
		goto out;

	/* Get a volume representation */
	server = nfs_create_server(&data, &mntfh);
	if (IS_ERR(server)) {
		error = PTR_ERR(server);
		goto out;
	}
	sb_mntdata.server = server;

	if (server->flags & NFS_MOUNT_UNSHARED)
		compare_super = NULL;

	/* Get a superblock - note that we may end up sharing one that already exists */
	s = sget(fs_type, compare_super, nfs_set_super, &sb_mntdata);
	if (IS_ERR(s)) {
		error = PTR_ERR(s);
		goto out_err_nosb;
	}

	if (s->s_fs_info != server) {
		nfs_free_server(server);
		server = NULL;
	}

	if (!s->s_root) {
		/* initial superblock/root creation */
		nfs_fill_super(s, &data);
	}

	mntroot = nfs_get_root(s, &mntfh);
	if (IS_ERR(mntroot)) {
		error = PTR_ERR(mntroot);
		goto error_splat_super;
	}

	error = security_sb_set_mnt_opts(s, &data.lsm_opts);
	if (error)
		goto error_splat_root;

	s->s_flags |= MS_ACTIVE;
	mnt->mnt_sb = s;
	mnt->mnt_root = mntroot;
	error = 0;

out:
	kfree(data.nfs_server.hostname);
	kfree(data.mount_server.hostname);
	security_free_mnt_opts(&data.lsm_opts);
	return error;

out_err_nosb:
	nfs_free_server(server);
	goto out;

error_splat_root:
	dput(mntroot);
error_splat_super:
	up_write(&s->s_umount);
	deactivate_super(s);
	goto out;
}

/*
 * Destroy an NFS2/3 superblock
 */
static void nfs_kill_super(struct super_block *s)
{
	struct nfs_server *server = NFS_SB(s);

	kill_anon_super(s);
	nfs_free_server(server);
}

/*
 * Clone an NFS2/3 server record on xdev traversal (FSID-change)
 */
static int nfs_xdev_get_sb(struct file_system_type *fs_type, int flags,
			   const char *dev_name, void *raw_data,
			   struct vfsmount *mnt)
{
	struct nfs_clone_mount *data = raw_data;
	struct super_block *s;
	struct nfs_server *server;
	struct dentry *mntroot;
	int (*compare_super)(struct super_block *, void *) = nfs_compare_super;
	struct nfs_sb_mountdata sb_mntdata = {
		.mntflags = flags,
	};
	int error;

	dprintk("--> nfs_xdev_get_sb()\n");

	/* create a new volume representation */
	server = nfs_clone_server(NFS_SB(data->sb), data->fh, data->fattr);
	if (IS_ERR(server)) {
		error = PTR_ERR(server);
		goto out_err_noserver;
	}
	sb_mntdata.server = server;

	if (server->flags & NFS_MOUNT_UNSHARED)
		compare_super = NULL;

	/* Get a superblock - note that we may end up sharing one that already exists */
	s = sget(&nfs_fs_type, compare_super, nfs_set_super, &sb_mntdata);
	if (IS_ERR(s)) {
		error = PTR_ERR(s);
		goto out_err_nosb;
	}

	if (s->s_fs_info != server) {
		nfs_free_server(server);
		server = NULL;
	}

	if (!s->s_root) {
		/* initial superblock/root creation */
		nfs_clone_super(s, data->sb);
	}

	mntroot = nfs_get_root(s, data->fh);
	if (IS_ERR(mntroot)) {
		error = PTR_ERR(mntroot);
		goto error_splat_super;
	}
	if (mntroot->d_inode->i_op != NFS_SB(s)->nfs_client->rpc_ops->dir_inode_ops) {
		dput(mntroot);
		error = -ESTALE;
		goto error_splat_super;
	}

	s->s_flags |= MS_ACTIVE;
	mnt->mnt_sb = s;
	mnt->mnt_root = mntroot;

	/* clone any lsm security options from the parent to the new sb */
	security_sb_clone_mnt_opts(data->sb, s);

	dprintk("<-- nfs_xdev_get_sb() = 0\n");
	return 0;

out_err_nosb:
	nfs_free_server(server);
out_err_noserver:
	dprintk("<-- nfs_xdev_get_sb() = %d [error]\n", error);
	return error;

error_splat_super:
	up_write(&s->s_umount);
	deactivate_super(s);
	dprintk("<-- nfs_xdev_get_sb() = %d [splat]\n", error);
	return error;
}

#ifdef CONFIG_NFS_V4

/*
 * Finish setting up a cloned NFS4 superblock
 */
static void nfs4_clone_super(struct super_block *sb,
			    const struct super_block *old_sb)
{
	sb->s_blocksize_bits = old_sb->s_blocksize_bits;
	sb->s_blocksize = old_sb->s_blocksize;
	sb->s_maxbytes = old_sb->s_maxbytes;
	sb->s_time_gran = 1;
	sb->s_op = old_sb->s_op;
 	nfs_initialise_sb(sb);
}

/*
 * Set up an NFS4 superblock
 */
static void nfs4_fill_super(struct super_block *sb)
{
	sb->s_time_gran = 1;
	sb->s_op = &nfs4_sops;
	nfs_initialise_sb(sb);
}

/*
 * Validate NFSv4 mount options
 */
static int nfs4_validate_mount_data(void *options,
				    struct nfs_parsed_mount_data *args,
				    const char *dev_name)
{
	struct sockaddr_in *ap;
	struct nfs4_mount_data *data = (struct nfs4_mount_data *)options;
	char *c;

	memset(args, 0, sizeof(*args));

	if (data == NULL)
		goto out_no_data;

	args->rsize		= NFS_MAX_FILE_IO_SIZE;
	args->wsize		= NFS_MAX_FILE_IO_SIZE;
	args->timeo		= 600;
	args->retrans		= 2;
	args->acregmin		= 3;
	args->acregmax		= 60;
	args->acdirmin		= 30;
	args->acdirmax		= 60;
	args->nfs_server.port	= NFS_PORT; /* 2049 unless user set port= */
	args->nfs_server.protocol = XPRT_TRANSPORT_TCP;

	switch (data->version) {
	case 1:
		ap = (struct sockaddr_in *)&args->nfs_server.address;
		if (data->host_addrlen > sizeof(args->nfs_server.address))
			goto out_no_address;
		if (data->host_addrlen == 0)
			goto out_no_address;
		args->nfs_server.addrlen = data->host_addrlen;
		if (copy_from_user(ap, data->host_addr, data->host_addrlen))
			return -EFAULT;
		if (!nfs_verify_server_address((struct sockaddr *)
						&args->nfs_server.address))
			goto out_no_address;

		switch (data->auth_flavourlen) {
		case 0:
			args->auth_flavors[0] = RPC_AUTH_UNIX;
			break;
		case 1:
			if (copy_from_user(&args->auth_flavors[0],
					   data->auth_flavours,
					   sizeof(args->auth_flavors[0])))
				return -EFAULT;
			break;
		default:
			goto out_inval_auth;
		}

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

		break;
	default: {
		unsigned int len;

		if (nfs_parse_mount_options((char *)options, args) == 0)
			return -EINVAL;

		if (!nfs_verify_server_address((struct sockaddr *)
						&args->nfs_server.address))
			return -EINVAL;

		switch (args->auth_flavor_len) {
		case 0:
			args->auth_flavors[0] = RPC_AUTH_UNIX;
			break;
		case 1:
			break;
		default:
			goto out_inval_auth;
		}

		/*
		 * Split "dev_name" into "hostname:mntpath".
		 */
		c = strchr(dev_name, ':');
		if (c == NULL)
			return -EINVAL;
		/* while calculating len, pretend ':' is '\0' */
		len = c - dev_name;
		if (len > NFS4_MAXNAMLEN)
			return -ENAMETOOLONG;
		/* N.B. caller will free nfs_server.hostname in all cases */
		args->nfs_server.hostname = kstrndup(dev_name, len, GFP_KERNEL);
		if (!args->nfs_server.hostname)
			goto out_nomem;

		c++;			/* step over the ':' */
		len = strlen(c);
		if (len > NFS4_MAXPATHLEN)
			return -ENAMETOOLONG;
		args->nfs_server.export_path = kstrndup(c, len, GFP_KERNEL);
		if (!args->nfs_server.export_path)
			goto out_nomem;

		dprintk("NFS: MNTPATH: '%s'\n", args->nfs_server.export_path);

		if (args->client_address == NULL)
			goto out_no_client_address;

		break;
		}
	}

	return 0;

out_no_data:
	dfprintk(MOUNT, "NFS4: mount program didn't pass any mount data\n");
	return -EINVAL;

out_inval_auth:
	dfprintk(MOUNT, "NFS4: Invalid number of RPC auth flavours %d\n",
		 data->auth_flavourlen);
	return -EINVAL;

out_nomem:
	dfprintk(MOUNT, "NFS4: not enough memory to handle mount options\n");
	return -ENOMEM;

out_no_address:
	dfprintk(MOUNT, "NFS4: mount program didn't pass remote address\n");
	return -EINVAL;

out_no_client_address:
	dfprintk(MOUNT, "NFS4: mount program didn't pass callback address\n");
	return -EINVAL;
}

/*
 * Get the superblock for an NFS4 mountpoint
 */
static int nfs4_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data, struct vfsmount *mnt)
{
	struct nfs_parsed_mount_data data;
	struct super_block *s;
	struct nfs_server *server;
	struct nfs_fh mntfh;
	struct dentry *mntroot;
	int (*compare_super)(struct super_block *, void *) = nfs_compare_super;
	struct nfs_sb_mountdata sb_mntdata = {
		.mntflags = flags,
	};
	int error;

	security_init_mnt_opts(&data.lsm_opts);

	/* Validate the mount data */
	error = nfs4_validate_mount_data(raw_data, &data, dev_name);
	if (error < 0)
		goto out;

	/* Get a volume representation */
	server = nfs4_create_server(&data, &mntfh);
	if (IS_ERR(server)) {
		error = PTR_ERR(server);
		goto out;
	}
	sb_mntdata.server = server;

	if (server->flags & NFS4_MOUNT_UNSHARED)
		compare_super = NULL;

	/* Get a superblock - note that we may end up sharing one that already exists */
	s = sget(fs_type, compare_super, nfs_set_super, &sb_mntdata);
	if (IS_ERR(s)) {
		error = PTR_ERR(s);
		goto out_free;
	}

	if (s->s_fs_info != server) {
		nfs_free_server(server);
		server = NULL;
	}

	if (!s->s_root) {
		/* initial superblock/root creation */
		nfs4_fill_super(s);
	}

	mntroot = nfs4_get_root(s, &mntfh);
	if (IS_ERR(mntroot)) {
		error = PTR_ERR(mntroot);
		goto error_splat_super;
	}

	s->s_flags |= MS_ACTIVE;
	mnt->mnt_sb = s;
	mnt->mnt_root = mntroot;
	error = 0;

out:
	kfree(data.client_address);
	kfree(data.nfs_server.export_path);
	kfree(data.nfs_server.hostname);
	security_free_mnt_opts(&data.lsm_opts);
	return error;

out_free:
	nfs_free_server(server);
	goto out;

error_splat_super:
	up_write(&s->s_umount);
	deactivate_super(s);
	goto out;
}

static void nfs4_kill_super(struct super_block *sb)
{
	struct nfs_server *server = NFS_SB(sb);

	nfs_return_all_delegations(sb);
	kill_anon_super(sb);

	nfs4_renewd_prepare_shutdown(server);
	nfs_free_server(server);
}

/*
 * Clone an NFS4 server record on xdev traversal (FSID-change)
 */
static int nfs4_xdev_get_sb(struct file_system_type *fs_type, int flags,
			    const char *dev_name, void *raw_data,
			    struct vfsmount *mnt)
{
	struct nfs_clone_mount *data = raw_data;
	struct super_block *s;
	struct nfs_server *server;
	struct dentry *mntroot;
	int (*compare_super)(struct super_block *, void *) = nfs_compare_super;
	struct nfs_sb_mountdata sb_mntdata = {
		.mntflags = flags,
	};
	int error;

	dprintk("--> nfs4_xdev_get_sb()\n");

	/* create a new volume representation */
	server = nfs_clone_server(NFS_SB(data->sb), data->fh, data->fattr);
	if (IS_ERR(server)) {
		error = PTR_ERR(server);
		goto out_err_noserver;
	}
	sb_mntdata.server = server;

	if (server->flags & NFS4_MOUNT_UNSHARED)
		compare_super = NULL;

	/* Get a superblock - note that we may end up sharing one that already exists */
	s = sget(&nfs_fs_type, compare_super, nfs_set_super, &sb_mntdata);
	if (IS_ERR(s)) {
		error = PTR_ERR(s);
		goto out_err_nosb;
	}

	if (s->s_fs_info != server) {
		nfs_free_server(server);
		server = NULL;
	}

	if (!s->s_root) {
		/* initial superblock/root creation */
		nfs4_clone_super(s, data->sb);
	}

	mntroot = nfs4_get_root(s, data->fh);
	if (IS_ERR(mntroot)) {
		error = PTR_ERR(mntroot);
		goto error_splat_super;
	}
	if (mntroot->d_inode->i_op != NFS_SB(s)->nfs_client->rpc_ops->dir_inode_ops) {
		dput(mntroot);
		error = -ESTALE;
		goto error_splat_super;
	}

	s->s_flags |= MS_ACTIVE;
	mnt->mnt_sb = s;
	mnt->mnt_root = mntroot;

	dprintk("<-- nfs4_xdev_get_sb() = 0\n");
	return 0;

out_err_nosb:
	nfs_free_server(server);
out_err_noserver:
	dprintk("<-- nfs4_xdev_get_sb() = %d [error]\n", error);
	return error;

error_splat_super:
	up_write(&s->s_umount);
	deactivate_super(s);
	dprintk("<-- nfs4_xdev_get_sb() = %d [splat]\n", error);
	return error;
}

/*
 * Create an NFS4 server record on referral traversal
 */
static int nfs4_referral_get_sb(struct file_system_type *fs_type, int flags,
				const char *dev_name, void *raw_data,
				struct vfsmount *mnt)
{
	struct nfs_clone_mount *data = raw_data;
	struct super_block *s;
	struct nfs_server *server;
	struct dentry *mntroot;
	struct nfs_fh mntfh;
	int (*compare_super)(struct super_block *, void *) = nfs_compare_super;
	struct nfs_sb_mountdata sb_mntdata = {
		.mntflags = flags,
	};
	int error;

	dprintk("--> nfs4_referral_get_sb()\n");

	/* create a new volume representation */
	server = nfs4_create_referral_server(data, &mntfh);
	if (IS_ERR(server)) {
		error = PTR_ERR(server);
		goto out_err_noserver;
	}
	sb_mntdata.server = server;

	if (server->flags & NFS4_MOUNT_UNSHARED)
		compare_super = NULL;

	/* Get a superblock - note that we may end up sharing one that already exists */
	s = sget(&nfs_fs_type, compare_super, nfs_set_super, &sb_mntdata);
	if (IS_ERR(s)) {
		error = PTR_ERR(s);
		goto out_err_nosb;
	}

	if (s->s_fs_info != server) {
		nfs_free_server(server);
		server = NULL;
	}

	if (!s->s_root) {
		/* initial superblock/root creation */
		nfs4_fill_super(s);
	}

	mntroot = nfs4_get_root(s, &mntfh);
	if (IS_ERR(mntroot)) {
		error = PTR_ERR(mntroot);
		goto error_splat_super;
	}
	if (mntroot->d_inode->i_op != NFS_SB(s)->nfs_client->rpc_ops->dir_inode_ops) {
		dput(mntroot);
		error = -ESTALE;
		goto error_splat_super;
	}

	s->s_flags |= MS_ACTIVE;
	mnt->mnt_sb = s;
	mnt->mnt_root = mntroot;

	dprintk("<-- nfs4_referral_get_sb() = 0\n");
	return 0;

out_err_nosb:
	nfs_free_server(server);
out_err_noserver:
	dprintk("<-- nfs4_referral_get_sb() = %d [error]\n", error);
	return error;

error_splat_super:
	up_write(&s->s_umount);
	deactivate_super(s);
	dprintk("<-- nfs4_referral_get_sb() = %d [splat]\n", error);
	return error;
}

#endif /* CONFIG_NFS_V4 */
