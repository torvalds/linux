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

#include <asm/system.h>
#include <asm/uaccess.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"
#include "iostat.h"
#include "internal.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

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
	shrink_submounts(vfsmnt, &nfs_automount_list);
}

/*
 * Validate the NFS2/NFS3 mount data
 * - fills in the mount root filehandle
 */
static int nfs_validate_mount_data(struct nfs_mount_data *data,
				   struct nfs_fh *mntfh)
{
	if (data == NULL) {
		dprintk("%s: missing data argument\n", __FUNCTION__);
		return -EINVAL;
	}

	if (data->version <= 0 || data->version > NFS_MOUNT_VERSION) {
		dprintk("%s: bad mount version\n", __FUNCTION__);
		return -EINVAL;
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
				return -EINVAL;
			}
			data->root.size = NFS2_FHSIZE;
			memcpy(data->root.data, data->old_root.data, NFS2_FHSIZE);
		case 4:
			if (data->flags & NFS_MOUNT_SECFLAVOUR) {
				dprintk("%s: mount structure version %d does not support strong security\n",
						__FUNCTION__,
						data->version);
				return -EINVAL;
			}
		case 5:
			memset(data->context, 0, sizeof(data->context));
	}

	/* Set the pseudoflavor */
	if (!(data->flags & NFS_MOUNT_SECFLAVOUR))
		data->pseudoflavor = RPC_AUTH_UNIX;

#ifndef CONFIG_NFS_V3
	/* If NFSv3 is not compiled in, return -EPROTONOSUPPORT */
	if (data->flags & NFS_MOUNT_VER3) {
		dprintk("%s: NFSv3 not compiled into kernel\n", __FUNCTION__);
		return -EPROTONOSUPPORT;
	}
#endif /* CONFIG_NFS_V3 */

	/* We now require that the mount process passes the remote address */
	if (data->addr.sin_addr.s_addr == INADDR_ANY) {
		dprintk("%s: mount program didn't pass remote address!\n",
			__FUNCTION__);
		return -EINVAL;
	}

	/* Prepare the root filehandle */
	if (data->flags & NFS_MOUNT_VER3)
		mntfh->size = data->root.size;
	else
		mntfh->size = NFS2_FHSIZE;

	if (mntfh->size > sizeof(mntfh->data)) {
		dprintk("%s: invalid root filehandle\n", __FUNCTION__);
		return -EINVAL;
	}

	memcpy(mntfh->data, data->root.data, mntfh->size);
	if (mntfh->size < sizeof(mntfh->data))
		memset(mntfh->data + mntfh->size, 0,
		       sizeof(mntfh->data) - mntfh->size);

	return 0;
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

	if (old->nfs_client != server->nfs_client)
		return 0;
	if (memcmp(&old->fsid, &server->fsid, sizeof(old->fsid)) != 0)
		return 0;
	return 1;
}

static int nfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data, struct vfsmount *mnt)
{
	struct nfs_server *server = NULL;
	struct super_block *s;
	struct nfs_fh mntfh;
	struct nfs_mount_data *data = raw_data;
	struct dentry *mntroot;
	int error;

	/* Validate the mount data */
	error = nfs_validate_mount_data(data, &mntfh);
	if (error < 0)
		return error;

	/* Get a volume representation */
	server = nfs_create_server(data, &mntfh);
	if (IS_ERR(server)) {
		error = PTR_ERR(server);
		goto out_err_noserver;
	}

	/* Get a superblock - note that we may end up sharing one that already exists */
	s = sget(fs_type, nfs_compare_super, nfs_set_super, server);
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
	return 0;

out_err_nosb:
	nfs_free_server(server);
out_err_noserver:
	return error;

error_splat_super:
	up_write(&s->s_umount);
	deactivate_super(s);
	return error;
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
	int error;

	dprintk("--> nfs_xdev_get_sb()\n");

	/* create a new volume representation */
	server = nfs_clone_server(NFS_SB(data->sb), data->fh, data->fattr);
	if (IS_ERR(server)) {
		error = PTR_ERR(server);
		goto out_err_noserver;
	}

	/* Get a superblock - note that we may end up sharing one that already exists */
	s = sget(&nfs_fs_type, nfs_compare_super, nfs_set_super, server);
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

static void *nfs_copy_user_string(char *dst, struct nfs_string *src, int maxlen)
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
	char *mntpath = NULL, *hostname = NULL, ip_addr[16];
	void *p;
	int error;

	if (data == NULL) {
		dprintk("%s: missing data argument\n", __FUNCTION__);
		return -EINVAL;
	}
	if (data->version <= 0 || data->version > NFS4_MOUNT_VERSION) {
		dprintk("%s: bad mount version\n", __FUNCTION__);
		return -EINVAL;
	}

	/* We now require that the mount process passes the remote address */
	if (data->host_addrlen != sizeof(addr))
		return -EINVAL;

	if (copy_from_user(&addr, data->host_addr, sizeof(addr)))
		return -EFAULT;

	if (addr.sin_family != AF_INET ||
	    addr.sin_addr.s_addr == INADDR_ANY
	    ) {
		dprintk("%s: mount program didn't pass remote IP address!\n",
				__FUNCTION__);
		return -EINVAL;
	}
	/* RFC3530: The default port for NFS is 2049 */
	if (addr.sin_port == 0)
		addr.sin_port = htons(NFS_PORT);

	/* Grab the authentication type */
	authflavour = RPC_AUTH_UNIX;
	if (data->auth_flavourlen != 0) {
		if (data->auth_flavourlen != 1) {
			dprintk("%s: Invalid number of RPC auth flavours %d.\n",
					__FUNCTION__, data->auth_flavourlen);
			error = -EINVAL;
			goto out_err_noserver;
		}

		if (copy_from_user(&authflavour, data->auth_flavours,
				   sizeof(authflavour))) {
			error = -EFAULT;
			goto out_err_noserver;
		}
	}

	p = nfs_copy_user_string(NULL, &data->hostname, 256);
	if (IS_ERR(p))
		goto out_err;
	hostname = p;

	p = nfs_copy_user_string(NULL, &data->mnt_path, 1024);
	if (IS_ERR(p))
		goto out_err;
	mntpath = p;

	dprintk("MNTPATH: %s\n", mntpath);

	p = nfs_copy_user_string(ip_addr, &data->client_addr,
				 sizeof(ip_addr) - 1);
	if (IS_ERR(p))
		goto out_err;

	/* Get a volume representation */
	server = nfs4_create_server(data, hostname, &addr, mntpath, ip_addr,
				    authflavour, &mntfh);
	if (IS_ERR(server)) {
		error = PTR_ERR(server);
		goto out_err_noserver;
	}

	/* Get a superblock - note that we may end up sharing one that already exists */
	s = sget(fs_type, nfs_compare_super, nfs_set_super, server);
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
	kfree(mntpath);
	kfree(hostname);
	return 0;

out_err:
	error = PTR_ERR(p);
	goto out_err_noserver;

out_free:
	nfs_free_server(server);
out_err_noserver:
	kfree(mntpath);
	kfree(hostname);
	return error;

error_splat_super:
	up_write(&s->s_umount);
	deactivate_super(s);
	goto out_err_noserver;
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
	int error;

	dprintk("--> nfs4_xdev_get_sb()\n");

	/* create a new volume representation */
	server = nfs_clone_server(NFS_SB(data->sb), data->fh, data->fattr);
	if (IS_ERR(server)) {
		error = PTR_ERR(server);
		goto out_err_noserver;
	}

	/* Get a superblock - note that we may end up sharing one that already exists */
	s = sget(&nfs_fs_type, nfs_compare_super, nfs_set_super, server);
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
	int error;

	dprintk("--> nfs4_referral_get_sb()\n");

	/* create a new volume representation */
	server = nfs4_create_referral_server(data, &mntfh);
	if (IS_ERR(server)) {
		error = PTR_ERR(server);
		goto out_err_noserver;
	}

	/* Get a superblock - note that we may end up sharing one that already exists */
	s = sget(&nfs_fs_type, nfs_compare_super, nfs_set_super, server);
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
