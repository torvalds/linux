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
 */

#include <linux/config.h>
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

#include <asm/system.h>
#include <asm/uaccess.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"
#include "iostat.h"
#include "internal.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

/* Maximum number of readahead requests
 * FIXME: this should really be a sysctl so that users may tune it to suit
 *        their needs. People that do NFS over a slow network, might for
 *        instance want to reduce it to something closer to 1 for improved
 *        interactive response.
 */
#define NFS_MAX_READAHEAD	(RPC_DEF_SLOT_TABLE - 1)

/*
 * RPC cruft for NFS
 */
static struct rpc_version * nfs_version[] = {
	NULL,
	NULL,
	&nfs_version2,
#if defined(CONFIG_NFS_V3)
	&nfs_version3,
#elif defined(CONFIG_NFS_V4)
	NULL,
#endif
#if defined(CONFIG_NFS_V4)
	&nfs_version4,
#endif
};

static struct rpc_program nfs_program = {
	.name			= "nfs",
	.number			= NFS_PROGRAM,
	.nrvers			= ARRAY_SIZE(nfs_version),
	.version		= nfs_version,
	.stats			= &nfs_rpcstat,
	.pipe_dir_name		= "/nfs",
};

struct rpc_stat nfs_rpcstat = {
	.program		= &nfs_program
};


#ifdef CONFIG_NFS_V3_ACL
static struct rpc_stat		nfsacl_rpcstat = { &nfsacl_program };
static struct rpc_version *	nfsacl_version[] = {
	[3]			= &nfsacl_version3,
};

struct rpc_program		nfsacl_program = {
	.name =			"nfsacl",
	.number =		NFS_ACL_PROGRAM,
	.nrvers =		ARRAY_SIZE(nfsacl_version),
	.version =		nfsacl_version,
	.stats =		&nfsacl_rpcstat,
};
#endif  /* CONFIG_NFS_V3_ACL */

static void nfs_umount_begin(struct vfsmount *, int);
static int  nfs_statfs(struct dentry *, struct kstatfs *);
static int  nfs_show_options(struct seq_file *, struct vfsmount *);
static int  nfs_show_stats(struct seq_file *, struct vfsmount *);
static int nfs_get_sb(struct file_system_type *, int, const char *, void *, struct vfsmount *);
static int nfs_clone_nfs_sb(struct file_system_type *fs_type,
		int flags, const char *dev_name, void *raw_data, struct vfsmount *mnt);
static void nfs_kill_super(struct super_block *);

static struct file_system_type nfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs",
	.get_sb		= nfs_get_sb,
	.kill_sb	= nfs_kill_super,
	.fs_flags	= FS_ODD_RENAME|FS_REVAL_DOT|FS_BINARY_MOUNTDATA,
};

struct file_system_type clone_nfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs",
	.get_sb		= nfs_clone_nfs_sb,
	.kill_sb	= nfs_kill_super,
	.fs_flags	= FS_ODD_RENAME|FS_REVAL_DOT|FS_BINARY_MOUNTDATA,
};

static struct super_operations nfs_sops = {
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
static int nfs_clone_nfs4_sb(struct file_system_type *fs_type,
		int flags, const char *dev_name, void *raw_data, struct vfsmount *mnt);
static int nfs_referral_nfs4_sb(struct file_system_type *fs_type,
		int flags, const char *dev_name, void *raw_data, struct vfsmount *mnt);
static void nfs4_kill_super(struct super_block *sb);

static struct file_system_type nfs4_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs4",
	.get_sb		= nfs4_get_sb,
	.kill_sb	= nfs4_kill_super,
	.fs_flags	= FS_ODD_RENAME|FS_REVAL_DOT|FS_BINARY_MOUNTDATA,
};

struct file_system_type clone_nfs4_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs4",
	.get_sb		= nfs_clone_nfs4_sb,
	.kill_sb	= nfs4_kill_super,
	.fs_flags	= FS_ODD_RENAME|FS_REVAL_DOT|FS_BINARY_MOUNTDATA,
};

struct file_system_type nfs_referral_nfs4_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs4",
	.get_sb		= nfs_referral_nfs4_sb,
	.kill_sb	= nfs4_kill_super,
	.fs_flags	= FS_ODD_RENAME|FS_REVAL_DOT|FS_BINARY_MOUNTDATA,
};

static struct super_operations nfs4_sops = {
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

#ifdef CONFIG_NFS_V4
	ret = nfs_register_sysctl();
	if (ret < 0)
		goto error_1;
	ret = register_filesystem(&nfs4_fs_type);
	if (ret < 0)
		goto error_2;
#endif
	acl_shrinker = set_shrinker(DEFAULT_SEEKS, nfs_access_cache_shrinker);
	return 0;

#ifdef CONFIG_NFS_V4
error_2:
	nfs_unregister_sysctl();
error_1:
	unregister_filesystem(&nfs_fs_type);
#endif
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

	error = server->rpc_ops->statfs(server, fh, &res);
	buf->f_type = NFS_SUPER_MAGIC;
	if (error < 0)
		goto out_err;

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
 out:
	unlock_kernel();
	return 0;

 out_err:
	dprintk("%s: statfs error = %d\n", __FUNCTION__, -error);
	buf->f_bsize = buf->f_blocks = buf->f_bfree = buf->f_bavail = -1;
	goto out;

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
		{ -1, "unknown" }
	};
	int i;

	for (i=0; sec_flavours[i].flavour != -1; i++) {
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
		{ 0, NULL, NULL }
	};
	const struct proc_nfs_info *nfs_infop;
	char buf[12];
	const char *proto;

	seq_printf(m, ",vers=%d", nfss->rpc_ops->version);
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
	seq_printf(m, ",timeo=%lu", 10U * nfss->retrans_timeo / HZ);
	seq_printf(m, ",retrans=%u", nfss->retrans_count);
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
	seq_escape(m, nfss->hostname, " \t\n\\");

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
	if (nfss->rpc_ops->version == 4) {
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
 * in response to traversals
 */
static void nfs_umount_begin(struct vfsmount *vfsmnt, int flags)
{
	struct nfs_server *server;
	struct rpc_clnt	*rpc;

	shrink_submounts(vfsmnt, &nfs_automount_list);
	if (!(flags & MNT_FORCE))
		return;
	/* -EIO all pending I/O */
	server = NFS_SB(vfsmnt->mnt_sb);
	rpc = server->client;
	if (!IS_ERR(rpc))
		rpc_killall_tasks(rpc);
	rpc = server->client_acl;
	if (!IS_ERR(rpc))
		rpc_killall_tasks(rpc);
}

/*
 * Obtain the root inode of the file system.
 */
static struct inode *
nfs_get_root(struct super_block *sb, struct nfs_fh *rootfh, struct nfs_fsinfo *fsinfo)
{
	struct nfs_server	*server = NFS_SB(sb);
	int			error;

	error = server->rpc_ops->getroot(server, rootfh, fsinfo);
	if (error < 0) {
		dprintk("nfs_get_root: getattr error = %d\n", -error);
		return ERR_PTR(error);
	}

	server->fsid = fsinfo->fattr->fsid;
	return nfs_fhget(sb, rootfh, fsinfo->fattr);
}

/*
 * Do NFS version-independent mount processing, and sanity checking
 */
static int
nfs_sb_init(struct super_block *sb, rpc_authflavor_t authflavor)
{
	struct nfs_server	*server;
	struct inode		*root_inode;
	struct nfs_fattr	fattr;
	struct nfs_fsinfo	fsinfo = {
					.fattr = &fattr,
				};
	struct nfs_pathconf pathinfo = {
			.fattr = &fattr,
	};
	int no_root_error = 0;
	unsigned long max_rpc_payload;

	/* We probably want something more informative here */
	snprintf(sb->s_id, sizeof(sb->s_id), "%x:%x", MAJOR(sb->s_dev), MINOR(sb->s_dev));

	server = NFS_SB(sb);

	sb->s_magic      = NFS_SUPER_MAGIC;

	server->io_stats = nfs_alloc_iostats();
	if (server->io_stats == NULL)
		return -ENOMEM;

	root_inode = nfs_get_root(sb, &server->fh, &fsinfo);
	/* Did getting the root inode fail? */
	if (IS_ERR(root_inode)) {
		no_root_error = PTR_ERR(root_inode);
		goto out_no_root;
	}
	sb->s_root = d_alloc_root(root_inode);
	if (!sb->s_root) {
		no_root_error = -ENOMEM;
		goto out_no_root;
	}
	sb->s_root->d_op = server->rpc_ops->dentry_ops;

	/* mount time stamp, in seconds */
	server->mount_time = jiffies;

	/* Get some general file system info */
	if (server->namelen == 0 &&
	    server->rpc_ops->pathconf(server, &server->fh, &pathinfo) >= 0)
		server->namelen = pathinfo.max_namelen;
	/* Work out a lot of parameters */
	if (server->rsize == 0)
		server->rsize = nfs_block_size(fsinfo.rtpref, NULL);
	if (server->wsize == 0)
		server->wsize = nfs_block_size(fsinfo.wtpref, NULL);

	if (fsinfo.rtmax >= 512 && server->rsize > fsinfo.rtmax)
		server->rsize = nfs_block_size(fsinfo.rtmax, NULL);
	if (fsinfo.wtmax >= 512 && server->wsize > fsinfo.wtmax)
		server->wsize = nfs_block_size(fsinfo.wtmax, NULL);

	max_rpc_payload = nfs_block_size(rpc_max_payload(server->client), NULL);
	if (server->rsize > max_rpc_payload)
		server->rsize = max_rpc_payload;
	if (server->rsize > NFS_MAX_FILE_IO_SIZE)
		server->rsize = NFS_MAX_FILE_IO_SIZE;
	server->rpages = (server->rsize + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;

	if (server->wsize > max_rpc_payload)
		server->wsize = max_rpc_payload;
	if (server->wsize > NFS_MAX_FILE_IO_SIZE)
		server->wsize = NFS_MAX_FILE_IO_SIZE;
	server->wpages = (server->wsize + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;

	if (sb->s_blocksize == 0)
		sb->s_blocksize = nfs_block_bits(server->wsize,
							 &sb->s_blocksize_bits);
	server->wtmult = nfs_block_bits(fsinfo.wtmult, NULL);

	server->dtsize = nfs_block_size(fsinfo.dtpref, NULL);
	if (server->dtsize > PAGE_CACHE_SIZE)
		server->dtsize = PAGE_CACHE_SIZE;
	if (server->dtsize > server->rsize)
		server->dtsize = server->rsize;

	if (server->flags & NFS_MOUNT_NOAC) {
		server->acregmin = server->acregmax = 0;
		server->acdirmin = server->acdirmax = 0;
		sb->s_flags |= MS_SYNCHRONOUS;
	}
	server->backing_dev_info.ra_pages = server->rpages * NFS_MAX_READAHEAD;

	nfs_super_set_maxbytes(sb, fsinfo.maxfilesize);

	server->client->cl_intr = (server->flags & NFS_MOUNT_INTR) ? 1 : 0;
	server->client->cl_softrtry = (server->flags & NFS_MOUNT_SOFT) ? 1 : 0;

	/* We're airborne Set socket buffersize */
	rpc_setbufsize(server->client, server->wsize + 100, server->rsize + 100);
	return 0;
	/* Yargs. It didn't work out. */
out_no_root:
	dprintk("nfs_sb_init: get root inode failed: errno %d\n", -no_root_error);
	if (!IS_ERR(root_inode))
		iput(root_inode);
	return no_root_error;
}

/*
 * Initialise the timeout values for a connection
 */
static void nfs_init_timeout_values(struct rpc_timeout *to, int proto, unsigned int timeo, unsigned int retrans)
{
	to->to_initval = timeo * HZ / 10;
	to->to_retries = retrans;
	if (!to->to_retries)
		to->to_retries = 2;

	switch (proto) {
	case IPPROTO_TCP:
		if (!to->to_initval)
			to->to_initval = 60 * HZ;
		if (to->to_initval > NFS_MAX_TCP_TIMEOUT)
			to->to_initval = NFS_MAX_TCP_TIMEOUT;
		to->to_increment = to->to_initval;
		to->to_maxval = to->to_initval + (to->to_increment * to->to_retries);
		to->to_exponential = 0;
		break;
	case IPPROTO_UDP:
	default:
		if (!to->to_initval)
			to->to_initval = 11 * HZ / 10;
		if (to->to_initval > NFS_MAX_UDP_TIMEOUT)
			to->to_initval = NFS_MAX_UDP_TIMEOUT;
		to->to_maxval = NFS_MAX_UDP_TIMEOUT;
		to->to_exponential = 1;
		break;
	}
}

/*
 * Create an RPC client handle.
 */
static struct rpc_clnt *
nfs_create_client(struct nfs_server *server, const struct nfs_mount_data *data)
{
	struct rpc_timeout	timeparms;
	struct rpc_xprt		*xprt = NULL;
	struct rpc_clnt		*clnt = NULL;
	int			proto = (data->flags & NFS_MOUNT_TCP) ? IPPROTO_TCP : IPPROTO_UDP;

	nfs_init_timeout_values(&timeparms, proto, data->timeo, data->retrans);

	server->retrans_timeo = timeparms.to_initval;
	server->retrans_count = timeparms.to_retries;

	/* create transport and client */
	xprt = xprt_create_proto(proto, &server->addr, &timeparms);
	if (IS_ERR(xprt)) {
		dprintk("%s: cannot create RPC transport. Error = %ld\n",
				__FUNCTION__, PTR_ERR(xprt));
		return (struct rpc_clnt *)xprt;
	}
	clnt = rpc_create_client(xprt, server->hostname, &nfs_program,
				 server->rpc_ops->version, data->pseudoflavor);
	if (IS_ERR(clnt)) {
		dprintk("%s: cannot create RPC client. Error = %ld\n",
				__FUNCTION__, PTR_ERR(xprt));
		goto out_fail;
	}

	clnt->cl_intr     = 1;
	clnt->cl_softrtry = 1;

	return clnt;

out_fail:
	return clnt;
}

/*
 * Clone a server record
 */
static struct nfs_server *nfs_clone_server(struct super_block *sb, struct nfs_clone_mount *data)
{
	struct nfs_server *server = NFS_SB(sb);
	struct nfs_server *parent = NFS_SB(data->sb);
	struct inode *root_inode;
	struct nfs_fsinfo fsinfo;
	void *err = ERR_PTR(-ENOMEM);

	sb->s_op = data->sb->s_op;
	sb->s_blocksize = data->sb->s_blocksize;
	sb->s_blocksize_bits = data->sb->s_blocksize_bits;
	sb->s_maxbytes = data->sb->s_maxbytes;

	server->client_sys = server->client_acl = ERR_PTR(-EINVAL);
	server->io_stats = nfs_alloc_iostats();
	if (server->io_stats == NULL)
		goto out;

	server->client = rpc_clone_client(parent->client);
	if (IS_ERR((err = server->client)))
		goto out;

	if (!IS_ERR(parent->client_sys)) {
		server->client_sys = rpc_clone_client(parent->client_sys);
		if (IS_ERR((err = server->client_sys)))
			goto out;
	}
	if (!IS_ERR(parent->client_acl)) {
		server->client_acl = rpc_clone_client(parent->client_acl);
		if (IS_ERR((err = server->client_acl)))
			goto out;
	}
	root_inode = nfs_fhget(sb, data->fh, data->fattr);
	if (!root_inode)
		goto out;
	sb->s_root = d_alloc_root(root_inode);
	if (!sb->s_root)
		goto out_put_root;
	fsinfo.fattr = data->fattr;
	if (NFS_PROTO(root_inode)->fsinfo(server, data->fh, &fsinfo) == 0)
		nfs_super_set_maxbytes(sb, fsinfo.maxfilesize);
	sb->s_root->d_op = server->rpc_ops->dentry_ops;
	sb->s_flags |= MS_ACTIVE;
	return server;
out_put_root:
	iput(root_inode);
out:
	return err;
}

/*
 * Copy an existing superblock and attach revised data
 */
static int nfs_clone_generic_sb(struct nfs_clone_mount *data,
		struct super_block *(*fill_sb)(struct nfs_server *, struct nfs_clone_mount *),
		struct nfs_server *(*fill_server)(struct super_block *, struct nfs_clone_mount *),
		struct vfsmount *mnt)
{
	struct nfs_server *server;
	struct nfs_server *parent = NFS_SB(data->sb);
	struct super_block *sb = ERR_PTR(-EINVAL);
	char *hostname;
	int error = -ENOMEM;
	int len;

	server = kmalloc(sizeof(struct nfs_server), GFP_KERNEL);
	if (server == NULL)
		goto out_err;
	memcpy(server, parent, sizeof(*server));
	hostname = (data->hostname != NULL) ? data->hostname : parent->hostname;
	len = strlen(hostname) + 1;
	server->hostname = kmalloc(len, GFP_KERNEL);
	if (server->hostname == NULL)
		goto free_server;
	memcpy(server->hostname, hostname, len);
	error = rpciod_up();
	if (error != 0)
		goto free_hostname;

	sb = fill_sb(server, data);
	if (IS_ERR(sb)) {
		error = PTR_ERR(sb);
		goto kill_rpciod;
	}
		
	if (sb->s_root)
		goto out_rpciod_down;

	server = fill_server(sb, data);
	if (IS_ERR(server)) {
		error = PTR_ERR(server);
		goto out_deactivate;
	}
	return simple_set_mnt(mnt, sb);
out_deactivate:
	up_write(&sb->s_umount);
	deactivate_super(sb);
	return error;
out_rpciod_down:
	rpciod_down();
	kfree(server->hostname);
	kfree(server);
	return simple_set_mnt(mnt, sb);
kill_rpciod:
	rpciod_down();
free_hostname:
	kfree(server->hostname);
free_server:
	kfree(server);
out_err:
	return error;
}

/*
 * Set up an NFS2/3 superblock
 *
 * The way this works is that the mount process passes a structure
 * in the data argument which contains the server's IP address
 * and the root file handle obtained from the server's mount
 * daemon. We stash these away in the private superblock fields.
 */
static int
nfs_fill_super(struct super_block *sb, struct nfs_mount_data *data, int silent)
{
	struct nfs_server	*server;
	rpc_authflavor_t	authflavor;

	server           = NFS_SB(sb);
	sb->s_blocksize_bits = 0;
	sb->s_blocksize = 0;
	if (data->bsize)
		sb->s_blocksize = nfs_block_size(data->bsize, &sb->s_blocksize_bits);
	if (data->rsize)
		server->rsize = nfs_block_size(data->rsize, NULL);
	if (data->wsize)
		server->wsize = nfs_block_size(data->wsize, NULL);
	server->flags    = data->flags & NFS_MOUNT_FLAGMASK;

	server->acregmin = data->acregmin*HZ;
	server->acregmax = data->acregmax*HZ;
	server->acdirmin = data->acdirmin*HZ;
	server->acdirmax = data->acdirmax*HZ;

	/* Start lockd here, before we might error out */
	if (!(server->flags & NFS_MOUNT_NONLM))
		lockd_up();

	server->namelen  = data->namlen;
	server->hostname = kmalloc(strlen(data->hostname) + 1, GFP_KERNEL);
	if (!server->hostname)
		return -ENOMEM;
	strcpy(server->hostname, data->hostname);

	/* Check NFS protocol revision and initialize RPC op vector
	 * and file handle pool. */
#ifdef CONFIG_NFS_V3
	if (server->flags & NFS_MOUNT_VER3) {
		server->rpc_ops = &nfs_v3_clientops;
		server->caps |= NFS_CAP_READDIRPLUS;
	} else {
		server->rpc_ops = &nfs_v2_clientops;
	}
#else
	server->rpc_ops = &nfs_v2_clientops;
#endif

	/* Fill in pseudoflavor for mount version < 5 */
	if (!(data->flags & NFS_MOUNT_SECFLAVOUR))
		data->pseudoflavor = RPC_AUTH_UNIX;
	authflavor = data->pseudoflavor;	/* save for sb_init() */
	/* XXX maybe we want to add a server->pseudoflavor field */

	/* Create RPC client handles */
	server->client = nfs_create_client(server, data);
	if (IS_ERR(server->client))
		return PTR_ERR(server->client);
	/* RFC 2623, sec 2.3.2 */
	if (authflavor != RPC_AUTH_UNIX) {
		struct rpc_auth *auth;

		server->client_sys = rpc_clone_client(server->client);
		if (IS_ERR(server->client_sys))
			return PTR_ERR(server->client_sys);
		auth = rpcauth_create(RPC_AUTH_UNIX, server->client_sys);
		if (IS_ERR(auth))
			return PTR_ERR(auth);
	} else {
		atomic_inc(&server->client->cl_count);
		server->client_sys = server->client;
	}
	if (server->flags & NFS_MOUNT_VER3) {
#ifdef CONFIG_NFS_V3_ACL
		if (!(server->flags & NFS_MOUNT_NOACL)) {
			server->client_acl = rpc_bind_new_program(server->client, &nfsacl_program, 3);
			/* No errors! Assume that Sun nfsacls are supported */
			if (!IS_ERR(server->client_acl))
				server->caps |= NFS_CAP_ACLS;
		}
#else
		server->flags &= ~NFS_MOUNT_NOACL;
#endif /* CONFIG_NFS_V3_ACL */
		/*
		 * The VFS shouldn't apply the umask to mode bits. We will
		 * do so ourselves when necessary.
		 */
		sb->s_flags |= MS_POSIXACL;
		if (server->namelen == 0 || server->namelen > NFS3_MAXNAMLEN)
			server->namelen = NFS3_MAXNAMLEN;
		sb->s_time_gran = 1;
	} else {
		if (server->namelen == 0 || server->namelen > NFS2_MAXNAMLEN)
			server->namelen = NFS2_MAXNAMLEN;
	}

	sb->s_op = &nfs_sops;
	return nfs_sb_init(sb, authflavor);
}

static int nfs_set_super(struct super_block *s, void *data)
{
	s->s_fs_info = data;
	return set_anon_super(s, data);
}

static int nfs_compare_super(struct super_block *sb, void *data)
{
	struct nfs_server *server = data;
	struct nfs_server *old = NFS_SB(sb);

	if (old->addr.sin_addr.s_addr != server->addr.sin_addr.s_addr)
		return 0;
	if (old->addr.sin_port != server->addr.sin_port)
		return 0;
	return !nfs_compare_fh(&old->fh, &server->fh);
}

static int nfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data, struct vfsmount *mnt)
{
	int error;
	struct nfs_server *server = NULL;
	struct super_block *s;
	struct nfs_fh *root;
	struct nfs_mount_data *data = raw_data;

	error = -EINVAL;
	if (data == NULL) {
		dprintk("%s: missing data argument\n", __FUNCTION__);
		goto out_err_noserver;
	}
	if (data->version <= 0 || data->version > NFS_MOUNT_VERSION) {
		dprintk("%s: bad mount version\n", __FUNCTION__);
		goto out_err_noserver;
	}
	switch (data->version) {
		case 1:
			data->namlen = 0;
		case 2:
			data->bsize  = 0;
		case 3:
			if (data->flags & NFS_MOUNT_VER3) {
				dprintk("%s: mount structure version %d does not support NFSv3\n",
						__FUNCTION__,
						data->version);
				goto out_err_noserver;
			}
			data->root.size = NFS2_FHSIZE;
			memcpy(data->root.data, data->old_root.data, NFS2_FHSIZE);
		case 4:
			if (data->flags & NFS_MOUNT_SECFLAVOUR) {
				dprintk("%s: mount structure version %d does not support strong security\n",
						__FUNCTION__,
						data->version);
				goto out_err_noserver;
			}
		case 5:
			memset(data->context, 0, sizeof(data->context));
	}
#ifndef CONFIG_NFS_V3
	/* If NFSv3 is not compiled in, return -EPROTONOSUPPORT */
	error = -EPROTONOSUPPORT;
	if (data->flags & NFS_MOUNT_VER3) {
		dprintk("%s: NFSv3 not compiled into kernel\n", __FUNCTION__);
		goto out_err_noserver;
	}
#endif /* CONFIG_NFS_V3 */

	error = -ENOMEM;
	server = kzalloc(sizeof(struct nfs_server), GFP_KERNEL);
	if (!server)
		goto out_err_noserver;
	/* Zero out the NFS state stuff */
	init_nfsv4_state(server);
	server->client = server->client_sys = server->client_acl = ERR_PTR(-EINVAL);

	root = &server->fh;
	if (data->flags & NFS_MOUNT_VER3)
		root->size = data->root.size;
	else
		root->size = NFS2_FHSIZE;
	error = -EINVAL;
	if (root->size > sizeof(root->data)) {
		dprintk("%s: invalid root filehandle\n", __FUNCTION__);
		goto out_err;
	}
	memcpy(root->data, data->root.data, root->size);

	/* We now require that the mount process passes the remote address */
	memcpy(&server->addr, &data->addr, sizeof(server->addr));
	if (server->addr.sin_addr.s_addr == INADDR_ANY) {
		dprintk("%s: mount program didn't pass remote address!\n",
				__FUNCTION__);
		goto out_err;
	}

	/* Fire up rpciod if not yet running */
	error = rpciod_up();
	if (error < 0) {
		dprintk("%s: couldn't start rpciod! Error = %d\n",
				__FUNCTION__, error);
		goto out_err;
	}

	s = sget(fs_type, nfs_compare_super, nfs_set_super, server);
	if (IS_ERR(s)) {
		error = PTR_ERR(s);
		goto out_err_rpciod;
	}

	if (s->s_root)
		goto out_rpciod_down;

	s->s_flags = flags;

	error = nfs_fill_super(s, data, flags & MS_SILENT ? 1 : 0);
	if (error) {
		up_write(&s->s_umount);
		deactivate_super(s);
		return error;
	}
	s->s_flags |= MS_ACTIVE;
	return simple_set_mnt(mnt, s);

out_rpciod_down:
	rpciod_down();
	kfree(server);
	return simple_set_mnt(mnt, s);

out_err_rpciod:
	rpciod_down();
out_err:
	kfree(server);
out_err_noserver:
	return error;
}

static void nfs_kill_super(struct super_block *s)
{
	struct nfs_server *server = NFS_SB(s);

	kill_anon_super(s);

	if (!IS_ERR(server->client))
		rpc_shutdown_client(server->client);
	if (!IS_ERR(server->client_sys))
		rpc_shutdown_client(server->client_sys);
	if (!IS_ERR(server->client_acl))
		rpc_shutdown_client(server->client_acl);

	if (!(server->flags & NFS_MOUNT_NONLM))
		lockd_down();	/* release rpc.lockd */

	rpciod_down();		/* release rpciod */

	nfs_free_iostats(server->io_stats);
	kfree(server->hostname);
	kfree(server);
	nfs_release_automount_timer();
}

static struct super_block *nfs_clone_sb(struct nfs_server *server, struct nfs_clone_mount *data)
{
	struct super_block *sb;

	server->fsid = data->fattr->fsid;
	nfs_copy_fh(&server->fh, data->fh);
	sb = sget(&nfs_fs_type, nfs_compare_super, nfs_set_super, server);
	if (!IS_ERR(sb) && sb->s_root == NULL && !(server->flags & NFS_MOUNT_NONLM))
		lockd_up();
	return sb;
}

static int nfs_clone_nfs_sb(struct file_system_type *fs_type,
		int flags, const char *dev_name, void *raw_data, struct vfsmount *mnt)
{
	struct nfs_clone_mount *data = raw_data;
	return nfs_clone_generic_sb(data, nfs_clone_sb, nfs_clone_server, mnt);
}

#ifdef CONFIG_NFS_V4
static struct rpc_clnt *nfs4_create_client(struct nfs_server *server,
	struct rpc_timeout *timeparms, int proto, rpc_authflavor_t flavor)
{
	struct nfs_client *clp;
	struct rpc_xprt *xprt = NULL;
	struct rpc_clnt *clnt = NULL;
	int err = -EIO;

	clp = nfs_get_client(server->hostname, &server->addr, 4);
	if (!clp) {
		dprintk("%s: failed to create NFS4 client.\n", __FUNCTION__);
		return ERR_PTR(err);
	}

	/* Now create transport and client */
	if (clp->cl_cons_state == NFS_CS_INITING) {
		xprt = xprt_create_proto(proto, &server->addr, timeparms);
		if (IS_ERR(xprt)) {
			err = PTR_ERR(xprt);
			dprintk("%s: cannot create RPC transport. Error = %d\n",
					__FUNCTION__, err);
			goto client_init_error;
		}
		/* Bind to a reserved port! */
		xprt->resvport = 1;
		clnt = rpc_create_client(xprt, server->hostname, &nfs_program,
				server->rpc_ops->version, flavor);
		if (IS_ERR(clnt)) {
			err = PTR_ERR(clnt);
			dprintk("%s: cannot create RPC client. Error = %d\n",
					__FUNCTION__, err);
			goto client_init_error;
		}
		clnt->cl_intr     = 1;
		clnt->cl_softrtry = 1;
		clp->cl_rpcclient = clnt;
		memcpy(clp->cl_ipaddr, server->ip_addr, sizeof(clp->cl_ipaddr));
		err = nfs_idmap_new(clp);
		if (err < 0) {
			dprintk("%s: failed to create idmapper.\n",
				__FUNCTION__);
			goto client_init_error;
		}
		__set_bit(NFS_CS_IDMAP, &clp->cl_res_state);
		nfs_mark_client_ready(clp, 0);
	}

	clnt = rpc_clone_client(clp->cl_rpcclient);

	if (IS_ERR(clnt)) {
		dprintk("%s: cannot create RPC client. Error = %d\n",
				__FUNCTION__, err);
		return clnt;
	}

	if (clnt->cl_auth->au_flavor != flavor) {
		struct rpc_auth *auth;

		auth = rpcauth_create(flavor, clnt);
		if (IS_ERR(auth)) {
			dprintk("%s: couldn't create credcache!\n", __FUNCTION__);
			return (struct rpc_clnt *)auth;
		}
	}

	server->nfs_client = clp;
	down_write(&clp->cl_sem);
	list_add_tail(&server->nfs4_siblings, &clp->cl_superblocks);
	up_write(&clp->cl_sem);
	return clnt;

client_init_error:
	nfs_mark_client_ready(clp, err);
	nfs_put_client(clp);
	return ERR_PTR(err);
}

/*
 * Set up an NFS4 superblock
 */
static int nfs4_fill_super(struct super_block *sb, struct nfs4_mount_data *data, int silent)
{
	struct nfs_server *server;
	struct rpc_timeout timeparms;
	rpc_authflavor_t authflavour;
	int err = -EIO;

	sb->s_blocksize_bits = 0;
	sb->s_blocksize = 0;
	server = NFS_SB(sb);
	if (data->rsize != 0)
		server->rsize = nfs_block_size(data->rsize, NULL);
	if (data->wsize != 0)
		server->wsize = nfs_block_size(data->wsize, NULL);
	server->flags = data->flags & NFS_MOUNT_FLAGMASK;
	server->caps = NFS_CAP_ATOMIC_OPEN;

	server->acregmin = data->acregmin*HZ;
	server->acregmax = data->acregmax*HZ;
	server->acdirmin = data->acdirmin*HZ;
	server->acdirmax = data->acdirmax*HZ;

	server->rpc_ops = &nfs_v4_clientops;

	nfs_init_timeout_values(&timeparms, data->proto, data->timeo, data->retrans);

	server->retrans_timeo = timeparms.to_initval;
	server->retrans_count = timeparms.to_retries;

	/* Now create transport and client */
	authflavour = RPC_AUTH_UNIX;
	if (data->auth_flavourlen != 0) {
		if (data->auth_flavourlen != 1) {
			dprintk("%s: Invalid number of RPC auth flavours %d.\n",
					__FUNCTION__, data->auth_flavourlen);
			err = -EINVAL;
			goto out_fail;
		}
		if (copy_from_user(&authflavour, data->auth_flavours, sizeof(authflavour))) {
			err = -EFAULT;
			goto out_fail;
		}
	}

	server->client = nfs4_create_client(server, &timeparms, data->proto, authflavour);
	if (IS_ERR(server->client)) {
		err = PTR_ERR(server->client);
			dprintk("%s: cannot create RPC client. Error = %d\n",
					__FUNCTION__, err);
			goto out_fail;
	}

	sb->s_time_gran = 1;

	sb->s_op = &nfs4_sops;
	err = nfs_sb_init(sb, authflavour);

 out_fail:
	return err;
}

static int nfs4_compare_super(struct super_block *sb, void *data)
{
	struct nfs_server *server = data;
	struct nfs_server *old = NFS_SB(sb);

	if (strcmp(server->hostname, old->hostname) != 0)
		return 0;
	if (strcmp(server->mnt_path, old->mnt_path) != 0)
		return 0;
	return 1;
}

static void *
nfs_copy_user_string(char *dst, struct nfs_string *src, int maxlen)
{
	void *p = NULL;

	if (!src->len)
		return ERR_PTR(-EINVAL);
	if (src->len < maxlen)
		maxlen = src->len;
	if (dst == NULL) {
		p = dst = kmalloc(maxlen + 1, GFP_KERNEL);
		if (p == NULL)
			return ERR_PTR(-ENOMEM);
	}
	if (copy_from_user(dst, src->data, maxlen)) {
		kfree(p);
		return ERR_PTR(-EFAULT);
	}
	dst[maxlen] = '\0';
	return dst;
}

static int nfs4_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data, struct vfsmount *mnt)
{
	int error;
	struct nfs_server *server;
	struct super_block *s;
	struct nfs4_mount_data *data = raw_data;
	void *p;

	if (data == NULL) {
		dprintk("%s: missing data argument\n", __FUNCTION__);
		return -EINVAL;
	}
	if (data->version <= 0 || data->version > NFS4_MOUNT_VERSION) {
		dprintk("%s: bad mount version\n", __FUNCTION__);
		return -EINVAL;
	}

	server = kzalloc(sizeof(struct nfs_server), GFP_KERNEL);
	if (!server)
		return -ENOMEM;
	/* Zero out the NFS state stuff */
	init_nfsv4_state(server);
	server->client = server->client_sys = server->client_acl = ERR_PTR(-EINVAL);

	p = nfs_copy_user_string(NULL, &data->hostname, 256);
	if (IS_ERR(p))
		goto out_err;
	server->hostname = p;

	p = nfs_copy_user_string(NULL, &data->mnt_path, 1024);
	if (IS_ERR(p))
		goto out_err;
	server->mnt_path = p;

	p = nfs_copy_user_string(server->ip_addr, &data->client_addr,
			sizeof(server->ip_addr) - 1);
	if (IS_ERR(p))
		goto out_err;

	/* We now require that the mount process passes the remote address */
	if (data->host_addrlen != sizeof(server->addr)) {
		error = -EINVAL;
		goto out_free;
	}
	if (copy_from_user(&server->addr, data->host_addr, sizeof(server->addr))) {
		error = -EFAULT;
		goto out_free;
	}
	if (server->addr.sin_family != AF_INET ||
	    server->addr.sin_addr.s_addr == INADDR_ANY) {
		dprintk("%s: mount program didn't pass remote IP address!\n",
				__FUNCTION__);
		error = -EINVAL;
		goto out_free;
	}

	s = sget(fs_type, nfs4_compare_super, nfs_set_super, server);
	if (IS_ERR(s)) {
		error = PTR_ERR(s);
		goto out_free;
	}

	if (s->s_root) {
		kfree(server->mnt_path);
		kfree(server->hostname);
		kfree(server);
		return simple_set_mnt(mnt, s);
	}

	s->s_flags = flags;

	error = nfs4_fill_super(s, data, flags & MS_SILENT ? 1 : 0);
	if (error) {
		up_write(&s->s_umount);
		deactivate_super(s);
		return error;
	}
	s->s_flags |= MS_ACTIVE;
	return simple_set_mnt(mnt, s);
out_err:
	error = PTR_ERR(p);
out_free:
	kfree(server->mnt_path);
	kfree(server->hostname);
	kfree(server);
	return error;
}

static void nfs4_kill_super(struct super_block *sb)
{
	struct nfs_server *server = NFS_SB(sb);

	nfs_return_all_delegations(sb);
	kill_anon_super(sb);

	nfs4_renewd_prepare_shutdown(server);

	if (server->client != NULL && !IS_ERR(server->client))
		rpc_shutdown_client(server->client);

	destroy_nfsv4_state(server);

	nfs_free_iostats(server->io_stats);
	kfree(server->hostname);
	kfree(server);
	nfs_release_automount_timer();
}

/*
 * Constructs the SERVER-side path
 */
static inline char *nfs4_dup_path(const struct dentry *dentry)
{
	char *page = (char *) __get_free_page(GFP_USER);
	char *path;

	path = nfs4_path(dentry, page, PAGE_SIZE);
	if (!IS_ERR(path)) {
		int len = PAGE_SIZE + page - path;
		char *tmp = path;

		path = kmalloc(len, GFP_KERNEL);
		if (path)
			memcpy(path, tmp, len);
		else
			path = ERR_PTR(-ENOMEM);
	}
	free_page((unsigned long)page);
	return path;
}

static struct super_block *nfs4_clone_sb(struct nfs_server *server, struct nfs_clone_mount *data)
{
	const struct dentry *dentry = data->dentry;
	struct nfs_client *clp = server->nfs_client;
	struct super_block *sb;

	server->fsid = data->fattr->fsid;
	nfs_copy_fh(&server->fh, data->fh);
	server->mnt_path = nfs4_dup_path(dentry);
	if (IS_ERR(server->mnt_path)) {
		sb = (struct super_block *)server->mnt_path;
		goto err;
	}
	sb = sget(&nfs4_fs_type, nfs4_compare_super, nfs_set_super, server);
	if (IS_ERR(sb) || sb->s_root)
		goto free_path;
	nfs4_server_capabilities(server, &server->fh);

	down_write(&clp->cl_sem);
	atomic_inc(&clp->cl_count);
	list_add_tail(&server->nfs4_siblings, &clp->cl_superblocks);
	up_write(&clp->cl_sem);
	return sb;
free_path:
	kfree(server->mnt_path);
err:
	server->mnt_path = NULL;
	return sb;
}

static int nfs_clone_nfs4_sb(struct file_system_type *fs_type,
		int flags, const char *dev_name, void *raw_data, struct vfsmount *mnt)
{
	struct nfs_clone_mount *data = raw_data;
	return nfs_clone_generic_sb(data, nfs4_clone_sb, nfs_clone_server, mnt);
}

static struct super_block *nfs4_referral_sb(struct nfs_server *server, struct nfs_clone_mount *data)
{
	struct super_block *sb = ERR_PTR(-ENOMEM);
	int len;

	len = strlen(data->mnt_path) + 1;
	server->mnt_path = kmalloc(len, GFP_KERNEL);
	if (server->mnt_path == NULL)
		goto err;
	memcpy(server->mnt_path, data->mnt_path, len);
	memcpy(&server->addr, data->addr, sizeof(struct sockaddr_in));

	sb = sget(&nfs4_fs_type, nfs4_compare_super, nfs_set_super, server);
	if (IS_ERR(sb) || sb->s_root)
		goto free_path;
	return sb;
free_path:
	kfree(server->mnt_path);
err:
	server->mnt_path = NULL;
	return sb;
}

static struct nfs_server *nfs4_referral_server(struct super_block *sb, struct nfs_clone_mount *data)
{
	struct nfs_server *server = NFS_SB(sb);
	struct rpc_timeout timeparms;
	int proto, timeo, retrans;
	void *err;

	proto = IPPROTO_TCP;
	/* Since we are following a referral and there may be alternatives,
	   set the timeouts and retries to low values */
	timeo = 2;
	retrans = 1;
	nfs_init_timeout_values(&timeparms, proto, timeo, retrans);

	server->client = nfs4_create_client(server, &timeparms, proto, data->authflavor);
	if (IS_ERR((err = server->client)))
		goto out_err;

	sb->s_time_gran = 1;
	sb->s_op = &nfs4_sops;
	err = ERR_PTR(nfs_sb_init(sb, data->authflavor));
	if (!IS_ERR(err))
		return server;
out_err:
	return (struct nfs_server *)err;
}

static int nfs_referral_nfs4_sb(struct file_system_type *fs_type,
		int flags, const char *dev_name, void *raw_data, struct vfsmount *mnt)
{
	struct nfs_clone_mount *data = raw_data;
	return nfs_clone_generic_sb(data, nfs4_referral_sb, nfs4_referral_server, mnt);
}

#endif
