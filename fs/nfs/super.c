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


struct nfs_parsed_mount_data {
	int			flags;
	int			rsize, wsize;
	int			timeo, retrans;
	int			acregmin, acregmax,
				acdirmin, acdirmax;
	int			namlen;
	unsigned int		bsize;
	unsigned int		auth_flavor_len;
	rpc_authflavor_t	auth_flavors[1];
	char			*client_address;

	struct {
		struct sockaddr_in	address;
		unsigned int		program;
		unsigned int		version;
		unsigned short		port;
		int			protocol;
	} mount_server;

	struct {
		struct sockaddr_in	address;
		char			*hostname;
		char			*export_path;
		unsigned int		program;
		int			protocol;
	} nfs_server;
};

enum {
	/* Mount options that take no arguments */
	Opt_soft, Opt_hard,
	Opt_intr, Opt_nointr,
	Opt_posix, Opt_noposix,
	Opt_cto, Opt_nocto,
	Opt_ac, Opt_noac,
	Opt_lock, Opt_nolock,
	Opt_v2, Opt_v3,
	Opt_udp, Opt_tcp,
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
	Opt_mountprog, Opt_mountvers,
	Opt_nfsprog, Opt_nfsvers,

	/* Mount options that take string arguments */
	Opt_sec, Opt_proto, Opt_mountproto,
	Opt_addr, Opt_mounthost, Opt_clientaddr,

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
	{ Opt_mountprog, "mountprog=%u" },
	{ Opt_mountvers, "mountvers=%u" },
	{ Opt_nfsprog, "nfsprog=%u" },
	{ Opt_nfsvers, "nfsvers=%u" },
	{ Opt_nfsvers, "vers=%u" },

	{ Opt_sec, "sec=%s" },
	{ Opt_proto, "proto=%s" },
	{ Opt_mountproto, "mountproto=%s" },
	{ Opt_addr, "addr=%s" },
	{ Opt_clientaddr, "clientaddr=%s" },
	{ Opt_mounthost, "mounthost=%s" },

	{ Opt_err, NULL }
};

enum {
	Opt_xprt_udp, Opt_xprt_tcp,

	Opt_xprt_err
};

static match_table_t nfs_xprt_protocol_tokens = {
	{ Opt_xprt_udp, "udp" },
	{ Opt_xprt_tcp, "tcp" },

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

	ret = nfs_register_sysctl();
	if (ret < 0)
		goto error_1;
#ifdef CONFIG_NFS_V4
	ret = register_filesystem(&nfs4_fs_type);
	if (ret < 0)
		goto error_2;
#endif
	acl_shrinker = set_shrinker(DEFAULT_SEEKS, nfs_access_cache_shrinker);
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
	if (acl_shrinker != NULL)
		remove_shrinker(acl_shrinker);
#ifdef CONFIG_NFS_V4
	unregister_filesystem(&nfs4_fs_type);
	nfs_unregister_sysctl();
#endif
	unregister_filesystem(&nfs_fs_type);
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

/*
 * Describe the mount options in force on this server representation
 */
static void nfs_show_mount_options(struct seq_file *m, struct nfs_server *nfss, int showdefaults)
{
	static const struct proc_nfs_info {
		int flag;
		const char *str;
		const char *nostr;
	} nfs_info[] = {
		{ NFS_MOUNT_SOFT, ",soft", ",hard" },
		{ NFS_MOUNT_INTR, ",intr", "" },
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
	char buf[12];
	const char *proto;

	seq_printf(m, ",vers=%d", clp->rpc_ops->version);
	seq_printf(m, ",rsize=%d", nfss->rsize);
	seq_printf(m, ",wsize=%d", nfss->wsize);
	if (nfss->acregmin != 3*HZ || showdefaults)
		seq_printf(m, ",acregmin=%d", nfss->acregmin/HZ);
	if (nfss->acregmax != 60*HZ || showdefaults)
		seq_printf(m, ",acregmax=%d", nfss->acregmax/HZ);
	if (nfss->acdirmin != 30*HZ || showdefaults)
		seq_printf(m, ",acdirmin=%d", nfss->acdirmin/HZ);
	if (nfss->acdirmax != 60*HZ || showdefaults)
		seq_printf(m, ",acdirmax=%d", nfss->acdirmax/HZ);
	for (nfs_infop = nfs_info; nfs_infop->flag; nfs_infop++) {
		if (nfss->flags & nfs_infop->flag)
			seq_puts(m, nfs_infop->str);
		else
			seq_puts(m, nfs_infop->nostr);
	}
	switch (nfss->client->cl_xprt->prot) {
		case IPPROTO_TCP:
			proto = "tcp";
			break;
		case IPPROTO_UDP:
			proto = "udp";
			break;
		default:
			snprintf(buf, sizeof(buf), "%u", nfss->client->cl_xprt->prot);
			proto = buf;
	}
	seq_printf(m, ",proto=%s", proto);
	seq_printf(m, ",timeo=%lu", 10U * clp->retrans_timeo / HZ);
	seq_printf(m, ",retrans=%u", clp->retrans_count);
	seq_printf(m, ",sec=%s", nfs_pseudoflavour_to_name(nfss->client->cl_auth->au_flavor));
}

/*
 * Describe the mount options on this VFS mountpoint
 */
static int nfs_show_options(struct seq_file *m, struct vfsmount *mnt)
{
	struct nfs_server *nfss = NFS_SB(mnt->mnt_sb);

	nfs_show_mount_options(m, nfss, 0);

	seq_puts(m, ",addr=");
	seq_escape(m, nfss->nfs_client->cl_hostname, " \t\n\\");

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
	seq_printf(m, ",wtmult=%d", nfss->wtmult);
	seq_printf(m, ",dtsize=%d", nfss->dtsize);
	seq_printf(m, ",bsize=%d", nfss->bsize);
	seq_printf(m, ",namelen=%d", nfss->namelen);

#ifdef CONFIG_NFS_V4
	if (nfss->nfs_client->cl_nfsversion == 4) {
		seq_printf(m, "\n\tnfsv4:\t");
		seq_printf(m, "bm0=0x%x", nfss->attr_bitmask[0]);
		seq_printf(m, ",bm1=0x%x", nfss->attr_bitmask[1]);
		seq_printf(m, ",acl=0x%x", nfss->acl_bitmask);
	}
#endif

	/*
	 * Display security flavor in effect for this mount
	 */
	seq_printf(m, "\n\tsec:\tflavor=%d", auth->au_ops->au_flavor);
	if (auth->au_flavor)
		seq_printf(m, ",pseudoflavor=%d", auth->au_flavor);

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

	shrink_submounts(vfsmnt, &nfs_automount_list);

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
 * Sanity-check a server address provided by the mount command
 */
static int nfs_verify_server_address(struct sockaddr *addr)
{
	switch (addr->sa_family) {
	case AF_INET: {
		struct sockaddr_in *sa = (struct sockaddr_in *) addr;
		if (sa->sin_addr.s_addr != INADDR_ANY)
			return 1;
		break;
	}
	}

	return 0;
}

/*
 * Error-check and convert a string of mount options from user space into
 * a data structure
 */
static int nfs_parse_mount_options(char *raw,
				   struct nfs_parsed_mount_data *mnt)
{
	char *p, *string;

	if (!raw) {
		dfprintk(MOUNT, "NFS: mount options string was NULL.\n");
		return 1;
	}
	dfprintk(MOUNT, "NFS: nfs mount opts='%s'\n", raw);

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
			mnt->flags |= NFS_MOUNT_INTR;
			break;
		case Opt_nointr:
			mnt->flags &= ~NFS_MOUNT_INTR;
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
			mnt->nfs_server.protocol = IPPROTO_UDP;
			mnt->timeo = 7;
			mnt->retrans = 5;
			break;
		case Opt_tcp:
			mnt->flags |= NFS_MOUNT_TCP;
			mnt->nfs_server.protocol = IPPROTO_TCP;
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
			mnt->nfs_server.address.sin_port = htonl(option);
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
		case Opt_mountprog:
			if (match_int(args, &option))
				return 0;
			if (option < 0)
				return 0;
			mnt->mount_server.program = option;
			break;
		case Opt_mountvers:
			if (match_int(args, &option))
				return 0;
			if (option < 0)
				return 0;
			mnt->mount_server.version = option;
			break;
		case Opt_nfsprog:
			if (match_int(args, &option))
				return 0;
			if (option < 0)
				return 0;
			mnt->nfs_server.program = option;
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
			case Opt_udp:
				mnt->flags &= ~NFS_MOUNT_TCP;
				mnt->nfs_server.protocol = IPPROTO_UDP;
				mnt->timeo = 7;
				mnt->retrans = 5;
				break;
			case Opt_tcp:
				mnt->flags |= NFS_MOUNT_TCP;
				mnt->nfs_server.protocol = IPPROTO_TCP;
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
			case Opt_udp:
				mnt->mount_server.protocol = IPPROTO_UDP;
				break;
			case Opt_tcp:
				mnt->mount_server.protocol = IPPROTO_TCP;
				break;
			default:
				goto out_unrec_xprt;
			}
			break;
		case Opt_addr:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			mnt->nfs_server.address.sin_family = AF_INET;
			mnt->nfs_server.address.sin_addr.s_addr =
							in_aton(string);
			kfree(string);
			break;
		case Opt_clientaddr:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			mnt->client_address = string;
			break;
		case Opt_mounthost:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			mnt->mount_server.address.sin_family = AF_INET;
			mnt->mount_server.address.sin_addr.s_addr =
							in_aton(string);
			kfree(string);
			break;

		case Opt_userspace:
		case Opt_deprecated:
			break;

		default:
			goto out_unknown;
		}
	}

	return 1;

out_nomem:
	printk(KERN_INFO "NFS: not enough memory to parse option\n");
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
	struct sockaddr_in sin;
	int status;

	if (args->mount_server.version == 0) {
		if (args->flags & NFS_MOUNT_VER3)
			args->mount_server.version = NFS_MNT3_VERSION;
		else
			args->mount_server.version = NFS_MNT_VERSION;
	}

	/*
	 * Construct the mount server's address.
	 */
	if (args->mount_server.address.sin_addr.s_addr != INADDR_ANY)
		sin = args->mount_server.address;
	else
		sin = args->nfs_server.address;
	if (args->mount_server.port == 0) {
		status = rpcb_getport_sync(&sin,
					   args->mount_server.program,
					   args->mount_server.version,
					   args->mount_server.protocol);
		if (status < 0)
			goto out_err;
		sin.sin_port = htons(status);
	} else
		sin.sin_port = htons(args->mount_server.port);

	/*
	 * Now ask the mount server to map our export path
	 * to a file handle.
	 */
	status = nfs_mount((struct sockaddr *) &sin,
			   sizeof(sin),
			   args->nfs_server.hostname,
			   args->nfs_server.export_path,
			   args->mount_server.version,
			   args->mount_server.protocol,
			   root_fh);
	if (status < 0)
		goto out_err;

	return status;

out_err:
	dfprintk(MOUNT, "NFS: unable to contact server on host "
		 NIPQUAD_FMT "\n", NIPQUAD(sin.sin_addr.s_addr));
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
 *
 * XXX: as far as I can tell, changing the NFS program number is not
 *      supported in the NFS client.
 */
static int nfs_validate_mount_data(struct nfs_mount_data **options,
				   struct nfs_fh *mntfh,
				   const char *dev_name)
{
	struct nfs_mount_data *data = *options;

	if (data == NULL)
		goto out_no_data;

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
		break;
	default: {
		unsigned int len;
		char *c;
		int status;
		struct nfs_parsed_mount_data args = {
			.flags		= (NFS_MOUNT_VER3 | NFS_MOUNT_TCP),
			.rsize		= NFS_MAX_FILE_IO_SIZE,
			.wsize		= NFS_MAX_FILE_IO_SIZE,
			.timeo		= 600,
			.retrans	= 2,
			.acregmin	= 3,
			.acregmax	= 60,
			.acdirmin	= 30,
			.acdirmax	= 60,
			.mount_server.protocol = IPPROTO_UDP,
			.mount_server.program = NFS_MNT_PROGRAM,
			.nfs_server.protocol = IPPROTO_TCP,
			.nfs_server.program = NFS_PROGRAM,
		};

		if (nfs_parse_mount_options((char *) *options, &args) == 0)
			return -EINVAL;

		data = kzalloc(sizeof(*data), GFP_KERNEL);
		if (data == NULL)
			return -ENOMEM;

		/*
		 * NB: after this point, caller will free "data"
		 * if we return an error
		 */
		*options = data;

		c = strchr(dev_name, ':');
		if (c == NULL)
			return -EINVAL;
		len = c - dev_name - 1;
		if (len > sizeof(data->hostname))
			return -EINVAL;
		strncpy(data->hostname, dev_name, len);
		args.nfs_server.hostname = data->hostname;

		c++;
		if (strlen(c) > NFS_MAXPATHLEN)
			return -EINVAL;
		args.nfs_server.export_path = c;

		status = nfs_try_mount(&args, mntfh);
		if (status)
			return -EINVAL;

		/*
		 * Translate to nfs_mount_data, which nfs_fill_super
		 * can deal with.
		 */
		data->version		= 6;
		data->flags		= args.flags;
		data->rsize		= args.rsize;
		data->wsize		= args.wsize;
		data->timeo		= args.timeo;
		data->retrans		= args.retrans;
		data->acregmin		= args.acregmin;
		data->acregmax		= args.acregmax;
		data->acdirmin		= args.acdirmin;
		data->acdirmax		= args.acdirmax;
		data->addr		= args.nfs_server.address;
		data->namlen		= args.namlen;
		data->bsize		= args.bsize;
		data->pseudoflavor	= args.auth_flavors[0];

		break;
		}
	}

	if (!(data->flags & NFS_MOUNT_SECFLAVOUR))
		data->pseudoflavor = RPC_AUTH_UNIX;

#ifndef CONFIG_NFS_V3
	if (data->flags & NFS_MOUNT_VER3)
		goto out_v3_not_compiled;
#endif /* !CONFIG_NFS_V3 */

	if (!nfs_verify_server_address((struct sockaddr *) &data->addr))
		goto out_no_address;

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
static void nfs_fill_super(struct super_block *sb, struct nfs_mount_data *data)
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

static int nfs_set_super(struct super_block *s, void *_server)
{
	struct nfs_server *server = _server;
	int ret;

	s->s_fs_info = server;
	ret = set_anon_super(s, server);
	if (ret == 0)
		server->s_dev = s->s_dev;
	return ret;
}

static int nfs_compare_super(struct super_block *sb, void *data)
{
	struct nfs_server *server = data, *old = NFS_SB(sb);

	if (memcmp(&old->nfs_client->cl_addr,
				&server->nfs_client->cl_addr,
				sizeof(old->nfs_client->cl_addr)) != 0)
		return 0;
	/* Note: NFS_MOUNT_UNSHARED == NFS4_MOUNT_UNSHARED */
	if (old->flags & NFS_MOUNT_UNSHARED)
		return 0;
	if (memcmp(&old->fsid, &server->fsid, sizeof(old->fsid)) != 0)
		return 0;
	return 1;
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
	return 0;
Ebusy:
	return -EBUSY;
}

static int nfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data, struct vfsmount *mnt)
{
	struct nfs_server *server = NULL;
	struct super_block *s;
	struct nfs_fh mntfh;
	struct nfs_mount_data *data = raw_data;
	struct dentry *mntroot;
	int (*compare_super)(struct super_block *, void *) = nfs_compare_super;
	int error;

	/* Validate the mount data */
	error = nfs_validate_mount_data(&data, &mntfh, dev_name);
	if (error < 0)
		goto out;

	/* Get a volume representation */
	server = nfs_create_server(data, &mntfh);
	if (IS_ERR(server)) {
		error = PTR_ERR(server);
		goto out;
	}

	if (server->flags & NFS_MOUNT_UNSHARED)
		compare_super = NULL;

	/* Get a superblock - note that we may end up sharing one that already exists */
	s = sget(fs_type, compare_super, nfs_set_super, server);
	if (IS_ERR(s)) {
		error = PTR_ERR(s);
		goto out_err_nosb;
	}

	if (s->s_fs_info != server) {
		error = nfs_compare_mount_options(s, server, flags);
		nfs_free_server(server);
		server = NULL;
		if (error < 0)
			goto error_splat_super;
	}

	if (!s->s_root) {
		/* initial superblock/root creation */
		s->s_flags = flags;
		nfs_fill_super(s, data);
	}

	mntroot = nfs_get_root(s, &mntfh);
	if (IS_ERR(mntroot)) {
		error = PTR_ERR(mntroot);
		goto error_splat_super;
	}

	s->s_flags |= MS_ACTIVE;
	mnt->mnt_sb = s;
	mnt->mnt_root = mntroot;
	error = 0;

out:
	if (data != raw_data)
		kfree(data);
	return error;

out_err_nosb:
	nfs_free_server(server);
	goto out;

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
	int error;

	dprintk("--> nfs_xdev_get_sb()\n");

	/* create a new volume representation */
	server = nfs_clone_server(NFS_SB(data->sb), data->fh, data->fattr);
	if (IS_ERR(server)) {
		error = PTR_ERR(server);
		goto out_err_noserver;
	}

	if (server->flags & NFS_MOUNT_UNSHARED)
		compare_super = NULL;

	/* Get a superblock - note that we may end up sharing one that already exists */
	s = sget(&nfs_fs_type, compare_super, nfs_set_super, server);
	if (IS_ERR(s)) {
		error = PTR_ERR(s);
		goto out_err_nosb;
	}

	if (s->s_fs_info != server) {
		error = nfs_compare_mount_options(s, server, flags);
		nfs_free_server(server);
		server = NULL;
		if (error < 0)
			goto error_splat_super;
	}

	if (!s->s_root) {
		/* initial superblock/root creation */
		s->s_flags = flags;
		nfs_clone_super(s, data->sb);
	}

	mntroot = nfs_get_root(s, data->fh);
	if (IS_ERR(mntroot)) {
		error = PTR_ERR(mntroot);
		goto error_splat_super;
	}

	s->s_flags |= MS_ACTIVE;
	mnt->mnt_sb = s;
	mnt->mnt_root = mntroot;

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
static int nfs4_validate_mount_data(struct nfs4_mount_data **options,
				    const char *dev_name,
				    struct sockaddr_in *addr,
				    rpc_authflavor_t *authflavour,
				    char **hostname,
				    char **mntpath,
				    char **ip_addr)
{
	struct nfs4_mount_data *data = *options;
	char *c;

	if (data == NULL)
		goto out_no_data;

	switch (data->version) {
	case 1:
		if (data->host_addrlen != sizeof(*addr))
			goto out_no_address;
		if (copy_from_user(addr, data->host_addr, sizeof(*addr)))
			return -EFAULT;
		if (addr->sin_port == 0)
			addr->sin_port = htons(NFS_PORT);
		if (!nfs_verify_server_address((struct sockaddr *) addr))
			goto out_no_address;

		switch (data->auth_flavourlen) {
		case 0:
			*authflavour = RPC_AUTH_UNIX;
			break;
		case 1:
			if (copy_from_user(authflavour, data->auth_flavours,
					   sizeof(*authflavour)))
				return -EFAULT;
			break;
		default:
			goto out_inval_auth;
		}

		c = strndup_user(data->hostname.data, NFS4_MAXNAMLEN);
		if (IS_ERR(c))
			return PTR_ERR(c);
		*hostname = c;

		c = strndup_user(data->mnt_path.data, NFS4_MAXPATHLEN);
		if (IS_ERR(c))
			return PTR_ERR(c);
		*mntpath = c;
		dfprintk(MOUNT, "NFS: MNTPATH: '%s'\n", *mntpath);

		c = strndup_user(data->client_addr.data, 16);
		if (IS_ERR(c))
			return PTR_ERR(c);
		*ip_addr = c;

		break;
	default: {
		unsigned int len;
		struct nfs_parsed_mount_data args = {
			.rsize		= NFS_MAX_FILE_IO_SIZE,
			.wsize		= NFS_MAX_FILE_IO_SIZE,
			.timeo		= 600,
			.retrans	= 2,
			.acregmin	= 3,
			.acregmax	= 60,
			.acdirmin	= 30,
			.acdirmax	= 60,
			.nfs_server.protocol = IPPROTO_TCP,
		};

		if (nfs_parse_mount_options((char *) *options, &args) == 0)
			return -EINVAL;

		if (!nfs_verify_server_address((struct sockaddr *)
						&args.nfs_server.address))
			return -EINVAL;
		*addr = args.nfs_server.address;

		switch (args.auth_flavor_len) {
		case 0:
			*authflavour = RPC_AUTH_UNIX;
			break;
		case 1:
			*authflavour = (rpc_authflavor_t) args.auth_flavors[0];
			break;
		default:
			goto out_inval_auth;
		}

		/*
		 * Translate to nfs4_mount_data, which nfs4_fill_super
		 * can deal with.
		 */
		data = kzalloc(sizeof(*data), GFP_KERNEL);
		if (data == NULL)
			return -ENOMEM;
		*options = data;

		data->version	= 1;
		data->flags	= args.flags & NFS4_MOUNT_FLAGMASK;
		data->rsize	= args.rsize;
		data->wsize	= args.wsize;
		data->timeo	= args.timeo;
		data->retrans	= args.retrans;
		data->acregmin	= args.acregmin;
		data->acregmax	= args.acregmax;
		data->acdirmin	= args.acdirmin;
		data->acdirmax	= args.acdirmax;
		data->proto	= args.nfs_server.protocol;

		/*
		 * Split "dev_name" into "hostname:mntpath".
		 */
		c = strchr(dev_name, ':');
		if (c == NULL)
			return -EINVAL;
		/* while calculating len, pretend ':' is '\0' */
		len = c - dev_name;
		if (len > NFS4_MAXNAMLEN)
			return -EINVAL;
		*hostname = kzalloc(len, GFP_KERNEL);
		if (*hostname == NULL)
			return -ENOMEM;
		strncpy(*hostname, dev_name, len - 1);

		c++;			/* step over the ':' */
		len = strlen(c);
		if (len > NFS4_MAXPATHLEN)
			return -EINVAL;
		*mntpath = kzalloc(len + 1, GFP_KERNEL);
		if (*mntpath == NULL)
			return -ENOMEM;
		strncpy(*mntpath, c, len);

		dprintk("MNTPATH: %s\n", *mntpath);

		*ip_addr = args.client_address;

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

out_no_address:
	dfprintk(MOUNT, "NFS4: mount program didn't pass remote address\n");
	return -EINVAL;
}

/*
 * Get the superblock for an NFS4 mountpoint
 */
static int nfs4_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data, struct vfsmount *mnt)
{
	struct nfs4_mount_data *data = raw_data;
	struct super_block *s;
	struct nfs_server *server;
	struct sockaddr_in addr;
	rpc_authflavor_t authflavour;
	struct nfs_fh mntfh;
	struct dentry *mntroot;
	char *mntpath = NULL, *hostname = NULL, *ip_addr = NULL;
	int (*compare_super)(struct super_block *, void *) = nfs_compare_super;
	int error;

	/* Validate the mount data */
	error = nfs4_validate_mount_data(&data, dev_name, &addr, &authflavour,
					 &hostname, &mntpath, &ip_addr);
	if (error < 0)
		goto out;

	/* Get a volume representation */
	server = nfs4_create_server(data, hostname, &addr, mntpath, ip_addr,
				    authflavour, &mntfh);
	if (IS_ERR(server)) {
		error = PTR_ERR(server);
		goto out;
	}

	if (server->flags & NFS4_MOUNT_UNSHARED)
		compare_super = NULL;

	/* Get a superblock - note that we may end up sharing one that already exists */
	s = sget(fs_type, compare_super, nfs_set_super, server);
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
		s->s_flags = flags;
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
	kfree(ip_addr);
	kfree(mntpath);
	kfree(hostname);
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
	int error;

	dprintk("--> nfs4_xdev_get_sb()\n");

	/* create a new volume representation */
	server = nfs_clone_server(NFS_SB(data->sb), data->fh, data->fattr);
	if (IS_ERR(server)) {
		error = PTR_ERR(server);
		goto out_err_noserver;
	}

	if (server->flags & NFS4_MOUNT_UNSHARED)
		compare_super = NULL;

	/* Get a superblock - note that we may end up sharing one that already exists */
	s = sget(&nfs_fs_type, compare_super, nfs_set_super, server);
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
		s->s_flags = flags;
		nfs4_clone_super(s, data->sb);
	}

	mntroot = nfs4_get_root(s, data->fh);
	if (IS_ERR(mntroot)) {
		error = PTR_ERR(mntroot);
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
	int error;

	dprintk("--> nfs4_referral_get_sb()\n");

	/* create a new volume representation */
	server = nfs4_create_referral_server(data, &mntfh);
	if (IS_ERR(server)) {
		error = PTR_ERR(server);
		goto out_err_noserver;
	}

	if (server->flags & NFS4_MOUNT_UNSHARED)
		compare_super = NULL;

	/* Get a superblock - note that we may end up sharing one that already exists */
	s = sget(&nfs_fs_type, compare_super, nfs_set_super, server);
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
		s->s_flags = flags;
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
