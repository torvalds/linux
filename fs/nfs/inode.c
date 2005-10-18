/*
 *  linux/fs/nfs/inode.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  nfs inode and superblock handling functions
 *
 *  Modularised by Alan Cox <Alan.Cox@linux.org>, while hacking some
 *  experimental NFS changes. Modularisation taken straight from SYS5 fs.
 *
 *  Change to nfs_read_super() to permit NFS mounts to multi-homed hosts.
 *  J.S.Peatfield@damtp.cam.ac.uk
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
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs4_mount.h>
#include <linux/lockd/bind.h>
#include <linux/smp_lock.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/nfs_idmap.h>
#include <linux/vfs.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include "nfs4_fs.h"
#include "delegation.h"

#define NFSDBG_FACILITY		NFSDBG_VFS
#define NFS_PARANOIA 1

/* Maximum number of readahead requests
 * FIXME: this should really be a sysctl so that users may tune it to suit
 *        their needs. People that do NFS over a slow network, might for
 *        instance want to reduce it to something closer to 1 for improved
 *        interactive response.
 */
#define NFS_MAX_READAHEAD	(RPC_DEF_SLOT_TABLE - 1)

static void nfs_invalidate_inode(struct inode *);
static int nfs_update_inode(struct inode *, struct nfs_fattr *, unsigned long);

static struct inode *nfs_alloc_inode(struct super_block *sb);
static void nfs_destroy_inode(struct inode *);
static int nfs_write_inode(struct inode *,int);
static void nfs_delete_inode(struct inode *);
static void nfs_clear_inode(struct inode *);
static void nfs_umount_begin(struct super_block *);
static int  nfs_statfs(struct super_block *, struct kstatfs *);
static int  nfs_show_options(struct seq_file *, struct vfsmount *);
static void nfs_zap_acl_cache(struct inode *);

static struct rpc_program	nfs_program;

static struct super_operations nfs_sops = { 
	.alloc_inode	= nfs_alloc_inode,
	.destroy_inode	= nfs_destroy_inode,
	.write_inode	= nfs_write_inode,
	.delete_inode	= nfs_delete_inode,
	.statfs		= nfs_statfs,
	.clear_inode	= nfs_clear_inode,
	.umount_begin	= nfs_umount_begin,
	.show_options	= nfs_show_options,
};

/*
 * RPC cruft for NFS
 */
static struct rpc_stat		nfs_rpcstat = {
	.program		= &nfs_program
};
static struct rpc_version *	nfs_version[] = {
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

static struct rpc_program	nfs_program = {
	.name			= "nfs",
	.number			= NFS_PROGRAM,
	.nrvers			= sizeof(nfs_version) / sizeof(nfs_version[0]),
	.version		= nfs_version,
	.stats			= &nfs_rpcstat,
	.pipe_dir_name		= "/nfs",
};

#ifdef CONFIG_NFS_V3_ACL
static struct rpc_stat		nfsacl_rpcstat = { &nfsacl_program };
static struct rpc_version *	nfsacl_version[] = {
	[3]			= &nfsacl_version3,
};

struct rpc_program		nfsacl_program = {
	.name =			"nfsacl",
	.number =		NFS_ACL_PROGRAM,
	.nrvers =		sizeof(nfsacl_version) / sizeof(nfsacl_version[0]),
	.version =		nfsacl_version,
	.stats =		&nfsacl_rpcstat,
};
#endif  /* CONFIG_NFS_V3_ACL */

static inline unsigned long
nfs_fattr_to_ino_t(struct nfs_fattr *fattr)
{
	return nfs_fileid_to_ino_t(fattr->fileid);
}

static int
nfs_write_inode(struct inode *inode, int sync)
{
	int flags = sync ? FLUSH_WAIT : 0;
	int ret;

	ret = nfs_commit_inode(inode, flags);
	if (ret < 0)
		return ret;
	return 0;
}

static void
nfs_delete_inode(struct inode * inode)
{
	dprintk("NFS: delete_inode(%s/%ld)\n", inode->i_sb->s_id, inode->i_ino);

	truncate_inode_pages(&inode->i_data, 0);

	nfs_wb_all(inode);
	/*
	 * The following should never happen...
	 */
	if (nfs_have_writebacks(inode)) {
		printk(KERN_ERR "nfs_delete_inode: inode %ld has pending RPC requests\n", inode->i_ino);
	}

	clear_inode(inode);
}

static void
nfs_clear_inode(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct rpc_cred *cred;

	nfs_wb_all(inode);
	BUG_ON (!list_empty(&nfsi->open_files));
	nfs_zap_acl_cache(inode);
	cred = nfsi->cache_access.cred;
	if (cred)
		put_rpccred(cred);
	BUG_ON(atomic_read(&nfsi->data_updates) != 0);
}

void
nfs_umount_begin(struct super_block *sb)
{
	struct rpc_clnt	*rpc = NFS_SB(sb)->client;

	/* -EIO all pending I/O */
	if (!IS_ERR(rpc))
		rpc_killall_tasks(rpc);
	rpc = NFS_SB(sb)->client_acl;
	if (!IS_ERR(rpc))
		rpc_killall_tasks(rpc);
}


static inline unsigned long
nfs_block_bits(unsigned long bsize, unsigned char *nrbitsp)
{
	/* make sure blocksize is a power of two */
	if ((bsize & (bsize - 1)) || nrbitsp) {
		unsigned char	nrbits;

		for (nrbits = 31; nrbits && !(bsize & (1 << nrbits)); nrbits--)
			;
		bsize = 1 << nrbits;
		if (nrbitsp)
			*nrbitsp = nrbits;
	}

	return bsize;
}

/*
 * Calculate the number of 512byte blocks used.
 */
static inline unsigned long
nfs_calc_block_size(u64 tsize)
{
	loff_t used = (tsize + 511) >> 9;
	return (used > ULONG_MAX) ? ULONG_MAX : used;
}

/*
 * Compute and set NFS server blocksize
 */
static inline unsigned long
nfs_block_size(unsigned long bsize, unsigned char *nrbitsp)
{
	if (bsize < 1024)
		bsize = NFS_DEF_FILE_IO_BUFFER_SIZE;
	else if (bsize >= NFS_MAX_FILE_IO_BUFFER_SIZE)
		bsize = NFS_MAX_FILE_IO_BUFFER_SIZE;

	return nfs_block_bits(bsize, nrbitsp);
}

/*
 * Obtain the root inode of the file system.
 */
static struct inode *
nfs_get_root(struct super_block *sb, struct nfs_fh *rootfh, struct nfs_fsinfo *fsinfo)
{
	struct nfs_server	*server = NFS_SB(sb);
	struct inode *rooti;
	int			error;

	error = server->rpc_ops->getroot(server, rootfh, fsinfo);
	if (error < 0) {
		dprintk("nfs_get_root: getattr error = %d\n", -error);
		return ERR_PTR(error);
	}

	rooti = nfs_fhget(sb, rootfh, fsinfo->fattr);
	if (!rooti)
		return ERR_PTR(-ENOMEM);
	return rooti;
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
	if (server->wsize > max_rpc_payload)
		server->wsize = max_rpc_payload;

	server->rpages = (server->rsize + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	if (server->rpages > NFS_READ_MAXIOV) {
		server->rpages = NFS_READ_MAXIOV;
		server->rsize = server->rpages << PAGE_CACHE_SHIFT;
	}

	server->wpages = (server->wsize + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
        if (server->wpages > NFS_WRITE_MAXIOV) {
		server->wpages = NFS_WRITE_MAXIOV;
                server->wsize = server->wpages << PAGE_CACHE_SHIFT;
	}

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

	sb->s_maxbytes = fsinfo.maxfilesize;
	if (sb->s_maxbytes > MAX_LFS_FILESIZE) 
		sb->s_maxbytes = MAX_LFS_FILESIZE; 

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
 * Create an RPC client handle.
 */
static struct rpc_clnt *
nfs_create_client(struct nfs_server *server, const struct nfs_mount_data *data)
{
	struct rpc_timeout	timeparms;
	struct rpc_xprt		*xprt = NULL;
	struct rpc_clnt		*clnt = NULL;
	int			tcp   = (data->flags & NFS_MOUNT_TCP);

	/* Initialize timeout values */
	timeparms.to_initval = data->timeo * HZ / 10;
	timeparms.to_retries = data->retrans;
	timeparms.to_maxval  = tcp ? RPC_MAX_TCP_TIMEOUT : RPC_MAX_UDP_TIMEOUT;
	timeparms.to_exponential = 1;

	if (!timeparms.to_initval)
		timeparms.to_initval = (tcp ? 600 : 11) * HZ / 10;
	if (!timeparms.to_retries)
		timeparms.to_retries = 5;

	/* create transport and client */
	xprt = xprt_create_proto(tcp ? IPPROTO_TCP : IPPROTO_UDP,
				 &server->addr, &timeparms);
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
	clnt->cl_chatty   = 1;

	return clnt;

out_fail:
	return clnt;
}

/*
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

static int
nfs_statfs(struct super_block *sb, struct kstatfs *buf)
{
	struct nfs_server *server = NFS_SB(sb);
	unsigned char blockbits;
	unsigned long blockres;
	struct nfs_fh *rootfh = NFS_FH(sb->s_root->d_inode);
	struct nfs_fattr fattr;
	struct nfs_fsstat res = {
			.fattr = &fattr,
	};
	int error;

	lock_kernel();

	error = server->rpc_ops->statfs(server, rootfh, &res);
	buf->f_type = NFS_SUPER_MAGIC;
	if (error < 0)
		goto out_err;

	/*
	 * Current versions of glibc do not correctly handle the
	 * case where f_frsize != f_bsize.  Eventually we want to
	 * report the value of wtmult in this field.
	 */
	buf->f_frsize = sb->s_blocksize;

	/*
	 * On most *nix systems, f_blocks, f_bfree, and f_bavail
	 * are reported in units of f_frsize.  Linux hasn't had
	 * an f_frsize field in its statfs struct until recently,
	 * thus historically Linux's sys_statfs reports these
	 * fields in units of f_bsize.
	 */
	buf->f_bsize = sb->s_blocksize;
	blockbits = sb->s_blocksize_bits;
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
	printk(KERN_WARNING "nfs_statfs: statfs error = %d\n", -error);
	buf->f_bsize = buf->f_blocks = buf->f_bfree = buf->f_bavail = -1;
	goto out;

}

static int nfs_show_options(struct seq_file *m, struct vfsmount *mnt)
{
	static struct proc_nfs_info {
		int flag;
		char *str;
		char *nostr;
	} nfs_info[] = {
		{ NFS_MOUNT_SOFT, ",soft", ",hard" },
		{ NFS_MOUNT_INTR, ",intr", "" },
		{ NFS_MOUNT_POSIX, ",posix", "" },
		{ NFS_MOUNT_TCP, ",tcp", ",udp" },
		{ NFS_MOUNT_NOCTO, ",nocto", "" },
		{ NFS_MOUNT_NOAC, ",noac", "" },
		{ NFS_MOUNT_NONLM, ",nolock", ",lock" },
		{ NFS_MOUNT_NOACL, ",noacl", "" },
		{ 0, NULL, NULL }
	};
	struct proc_nfs_info *nfs_infop;
	struct nfs_server *nfss = NFS_SB(mnt->mnt_sb);

	seq_printf(m, ",v%d", nfss->rpc_ops->version);
	seq_printf(m, ",rsize=%d", nfss->rsize);
	seq_printf(m, ",wsize=%d", nfss->wsize);
	if (nfss->acregmin != 3*HZ)
		seq_printf(m, ",acregmin=%d", nfss->acregmin/HZ);
	if (nfss->acregmax != 60*HZ)
		seq_printf(m, ",acregmax=%d", nfss->acregmax/HZ);
	if (nfss->acdirmin != 30*HZ)
		seq_printf(m, ",acdirmin=%d", nfss->acdirmin/HZ);
	if (nfss->acdirmax != 60*HZ)
		seq_printf(m, ",acdirmax=%d", nfss->acdirmax/HZ);
	for (nfs_infop = nfs_info; nfs_infop->flag; nfs_infop++) {
		if (nfss->flags & nfs_infop->flag)
			seq_puts(m, nfs_infop->str);
		else
			seq_puts(m, nfs_infop->nostr);
	}
	seq_puts(m, ",addr=");
	seq_escape(m, nfss->hostname, " \t\n\\");
	return 0;
}

/*
 * Invalidate the local caches
 */
void
nfs_zap_caches(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int mode = inode->i_mode;

	spin_lock(&inode->i_lock);

	NFS_ATTRTIMEO(inode) = NFS_MINATTRTIMEO(inode);
	NFS_ATTRTIMEO_UPDATE(inode) = jiffies;

	memset(NFS_COOKIEVERF(inode), 0, sizeof(NFS_COOKIEVERF(inode)));
	if (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode))
		nfsi->cache_validity |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_DATA|NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL|NFS_INO_REVAL_PAGECACHE;
	else
		nfsi->cache_validity |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL|NFS_INO_REVAL_PAGECACHE;

	spin_unlock(&inode->i_lock);
}

static void nfs_zap_acl_cache(struct inode *inode)
{
	void (*clear_acl_cache)(struct inode *);

	clear_acl_cache = NFS_PROTO(inode)->clear_acl_cache;
	if (clear_acl_cache != NULL)
		clear_acl_cache(inode);
	spin_lock(&inode->i_lock);
	NFS_I(inode)->cache_validity &= ~NFS_INO_INVALID_ACL;
	spin_unlock(&inode->i_lock);
}

/*
 * Invalidate, but do not unhash, the inode
 */
static void
nfs_invalidate_inode(struct inode *inode)
{
	umode_t save_mode = inode->i_mode;

	make_bad_inode(inode);
	inode->i_mode = save_mode;
	nfs_zap_caches(inode);
}

struct nfs_find_desc {
	struct nfs_fh		*fh;
	struct nfs_fattr	*fattr;
};

/*
 * In NFSv3 we can have 64bit inode numbers. In order to support
 * this, and re-exported directories (also seen in NFSv2)
 * we are forced to allow 2 different inodes to have the same
 * i_ino.
 */
static int
nfs_find_actor(struct inode *inode, void *opaque)
{
	struct nfs_find_desc	*desc = (struct nfs_find_desc *)opaque;
	struct nfs_fh		*fh = desc->fh;
	struct nfs_fattr	*fattr = desc->fattr;

	if (NFS_FILEID(inode) != fattr->fileid)
		return 0;
	if (nfs_compare_fh(NFS_FH(inode), fh))
		return 0;
	if (is_bad_inode(inode) || NFS_STALE(inode))
		return 0;
	return 1;
}

static int
nfs_init_locked(struct inode *inode, void *opaque)
{
	struct nfs_find_desc	*desc = (struct nfs_find_desc *)opaque;
	struct nfs_fattr	*fattr = desc->fattr;

	NFS_FILEID(inode) = fattr->fileid;
	nfs_copy_fh(NFS_FH(inode), desc->fh);
	return 0;
}

/* Don't use READDIRPLUS on directories that we believe are too large */
#define NFS_LIMIT_READDIRPLUS (8*PAGE_SIZE)

/*
 * This is our front-end to iget that looks up inodes by file handle
 * instead of inode number.
 */
struct inode *
nfs_fhget(struct super_block *sb, struct nfs_fh *fh, struct nfs_fattr *fattr)
{
	struct nfs_find_desc desc = {
		.fh	= fh,
		.fattr	= fattr
	};
	struct inode *inode = NULL;
	unsigned long hash;

	if ((fattr->valid & NFS_ATTR_FATTR) == 0)
		goto out_no_inode;

	if (!fattr->nlink) {
		printk("NFS: Buggy server - nlink == 0!\n");
		goto out_no_inode;
	}

	hash = nfs_fattr_to_ino_t(fattr);

	if (!(inode = iget5_locked(sb, hash, nfs_find_actor, nfs_init_locked, &desc)))
		goto out_no_inode;

	if (inode->i_state & I_NEW) {
		struct nfs_inode *nfsi = NFS_I(inode);

		/* We set i_ino for the few things that still rely on it,
		 * such as stat(2) */
		inode->i_ino = hash;

		/* We can't support update_atime(), since the server will reset it */
		inode->i_flags |= S_NOATIME|S_NOCMTIME;
		inode->i_mode = fattr->mode;
		/* Why so? Because we want revalidate for devices/FIFOs, and
		 * that's precisely what we have in nfs_file_inode_operations.
		 */
		inode->i_op = NFS_SB(sb)->rpc_ops->file_inode_ops;
		if (S_ISREG(inode->i_mode)) {
			inode->i_fop = &nfs_file_operations;
			inode->i_data.a_ops = &nfs_file_aops;
			inode->i_data.backing_dev_info = &NFS_SB(sb)->backing_dev_info;
		} else if (S_ISDIR(inode->i_mode)) {
			inode->i_op = NFS_SB(sb)->rpc_ops->dir_inode_ops;
			inode->i_fop = &nfs_dir_operations;
			if (nfs_server_capable(inode, NFS_CAP_READDIRPLUS)
			    && fattr->size <= NFS_LIMIT_READDIRPLUS)
				set_bit(NFS_INO_ADVISE_RDPLUS, &NFS_FLAGS(inode));
		} else if (S_ISLNK(inode->i_mode))
			inode->i_op = &nfs_symlink_inode_operations;
		else
			init_special_inode(inode, inode->i_mode, fattr->rdev);

		nfsi->read_cache_jiffies = fattr->timestamp;
		inode->i_atime = fattr->atime;
		inode->i_mtime = fattr->mtime;
		inode->i_ctime = fattr->ctime;
		if (fattr->valid & NFS_ATTR_FATTR_V4)
			nfsi->change_attr = fattr->change_attr;
		inode->i_size = nfs_size_to_loff_t(fattr->size);
		inode->i_nlink = fattr->nlink;
		inode->i_uid = fattr->uid;
		inode->i_gid = fattr->gid;
		if (fattr->valid & (NFS_ATTR_FATTR_V3 | NFS_ATTR_FATTR_V4)) {
			/*
			 * report the blocks in 512byte units
			 */
			inode->i_blocks = nfs_calc_block_size(fattr->du.nfs3.used);
			inode->i_blksize = inode->i_sb->s_blocksize;
		} else {
			inode->i_blocks = fattr->du.nfs2.blocks;
			inode->i_blksize = fattr->du.nfs2.blocksize;
		}
		nfsi->attrtimeo = NFS_MINATTRTIMEO(inode);
		nfsi->attrtimeo_timestamp = jiffies;
		memset(nfsi->cookieverf, 0, sizeof(nfsi->cookieverf));
		nfsi->cache_access.cred = NULL;

		unlock_new_inode(inode);
	} else
		nfs_refresh_inode(inode, fattr);
	dprintk("NFS: nfs_fhget(%s/%Ld ct=%d)\n",
		inode->i_sb->s_id,
		(long long)NFS_FILEID(inode),
		atomic_read(&inode->i_count));

out:
	return inode;

out_no_inode:
	printk("nfs_fhget: iget failed\n");
	goto out;
}

#define NFS_VALID_ATTRS (ATTR_MODE|ATTR_UID|ATTR_GID|ATTR_SIZE|ATTR_ATIME|ATTR_ATIME_SET|ATTR_MTIME|ATTR_MTIME_SET)

int
nfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct nfs_fattr fattr;
	int error;

	if (attr->ia_valid & ATTR_SIZE) {
		if (!S_ISREG(inode->i_mode) || attr->ia_size == i_size_read(inode))
			attr->ia_valid &= ~ATTR_SIZE;
	}

	/* Optimization: if the end result is no change, don't RPC */
	attr->ia_valid &= NFS_VALID_ATTRS;
	if (attr->ia_valid == 0)
		return 0;

	lock_kernel();
	nfs_begin_data_update(inode);
	/* Write all dirty data if we're changing file permissions or size */
	if ((attr->ia_valid & (ATTR_MODE|ATTR_UID|ATTR_GID|ATTR_SIZE)) != 0) {
		if (filemap_fdatawrite(inode->i_mapping) == 0)
			filemap_fdatawait(inode->i_mapping);
		nfs_wb_all(inode);
	}
	error = NFS_PROTO(inode)->setattr(dentry, &fattr, attr);
	if (error == 0)
		nfs_refresh_inode(inode, &fattr);
	nfs_end_data_update(inode);
	unlock_kernel();
	return error;
}

/**
 * nfs_setattr_update_inode - Update inode metadata after a setattr call.
 * @inode: pointer to struct inode
 * @attr: pointer to struct iattr
 *
 * Note: we do this in the *proc.c in order to ensure that
 *       it works for things like exclusive creates too.
 */
void nfs_setattr_update_inode(struct inode *inode, struct iattr *attr)
{
	if ((attr->ia_valid & (ATTR_MODE|ATTR_UID|ATTR_GID)) != 0) {
		if ((attr->ia_valid & ATTR_MODE) != 0) {
			int mode = attr->ia_mode & S_IALLUGO;
			mode |= inode->i_mode & ~S_IALLUGO;
			inode->i_mode = mode;
		}
		if ((attr->ia_valid & ATTR_UID) != 0)
			inode->i_uid = attr->ia_uid;
		if ((attr->ia_valid & ATTR_GID) != 0)
			inode->i_gid = attr->ia_gid;
		spin_lock(&inode->i_lock);
		NFS_I(inode)->cache_validity |= NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL;
		spin_unlock(&inode->i_lock);
	}
	if ((attr->ia_valid & ATTR_SIZE) != 0) {
		inode->i_size = attr->ia_size;
		vmtruncate(inode, attr->ia_size);
	}
}

static int nfs_wait_schedule(void *word)
{
	if (signal_pending(current))
		return -ERESTARTSYS;
	schedule();
	return 0;
}

/*
 * Wait for the inode to get unlocked.
 */
static int nfs_wait_on_inode(struct inode *inode)
{
	struct rpc_clnt	*clnt = NFS_CLIENT(inode);
	struct nfs_inode *nfsi = NFS_I(inode);
	sigset_t oldmask;
	int error;

	rpc_clnt_sigmask(clnt, &oldmask);
	error = wait_on_bit_lock(&nfsi->flags, NFS_INO_REVALIDATING,
					nfs_wait_schedule, TASK_INTERRUPTIBLE);
	rpc_clnt_sigunmask(clnt, &oldmask);

	return error;
}

static void nfs_wake_up_inode(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	clear_bit(NFS_INO_REVALIDATING, &nfsi->flags);
	smp_mb__after_clear_bit();
	wake_up_bit(&nfsi->flags, NFS_INO_REVALIDATING);
}

int nfs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	int need_atime = NFS_I(inode)->cache_validity & NFS_INO_INVALID_ATIME;
	int err;

	if (__IS_FLG(inode, MS_NOATIME))
		need_atime = 0;
	else if (__IS_FLG(inode, MS_NODIRATIME) && S_ISDIR(inode->i_mode))
		need_atime = 0;
	/* We may force a getattr if the user cares about atime */
	if (need_atime)
		err = __nfs_revalidate_inode(NFS_SERVER(inode), inode);
	else
		err = nfs_revalidate_inode(NFS_SERVER(inode), inode);
	if (!err)
		generic_fillattr(inode, stat);
	return err;
}

struct nfs_open_context *alloc_nfs_open_context(struct dentry *dentry, struct rpc_cred *cred)
{
	struct nfs_open_context *ctx;

	ctx = (struct nfs_open_context *)kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx != NULL) {
		atomic_set(&ctx->count, 1);
		ctx->dentry = dget(dentry);
		ctx->cred = get_rpccred(cred);
		ctx->state = NULL;
		ctx->lockowner = current->files;
		ctx->error = 0;
		ctx->dir_cookie = 0;
	}
	return ctx;
}

struct nfs_open_context *get_nfs_open_context(struct nfs_open_context *ctx)
{
	if (ctx != NULL)
		atomic_inc(&ctx->count);
	return ctx;
}

void put_nfs_open_context(struct nfs_open_context *ctx)
{
	if (atomic_dec_and_test(&ctx->count)) {
		if (!list_empty(&ctx->list)) {
			struct inode *inode = ctx->dentry->d_inode;
			spin_lock(&inode->i_lock);
			list_del(&ctx->list);
			spin_unlock(&inode->i_lock);
		}
		if (ctx->state != NULL)
			nfs4_close_state(ctx->state, ctx->mode);
		if (ctx->cred != NULL)
			put_rpccred(ctx->cred);
		dput(ctx->dentry);
		kfree(ctx);
	}
}

/*
 * Ensure that mmap has a recent RPC credential for use when writing out
 * shared pages
 */
void nfs_file_set_open_context(struct file *filp, struct nfs_open_context *ctx)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct nfs_inode *nfsi = NFS_I(inode);

	filp->private_data = get_nfs_open_context(ctx);
	spin_lock(&inode->i_lock);
	list_add(&ctx->list, &nfsi->open_files);
	spin_unlock(&inode->i_lock);
}

struct nfs_open_context *nfs_find_open_context(struct inode *inode, int mode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_open_context *pos, *ctx = NULL;

	spin_lock(&inode->i_lock);
	list_for_each_entry(pos, &nfsi->open_files, list) {
		if ((pos->mode & mode) == mode) {
			ctx = get_nfs_open_context(pos);
			break;
		}
	}
	spin_unlock(&inode->i_lock);
	return ctx;
}

void nfs_file_clear_open_context(struct file *filp)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct nfs_open_context *ctx = (struct nfs_open_context *)filp->private_data;

	if (ctx) {
		filp->private_data = NULL;
		spin_lock(&inode->i_lock);
		list_move_tail(&ctx->list, &NFS_I(inode)->open_files);
		spin_unlock(&inode->i_lock);
		put_nfs_open_context(ctx);
	}
}

/*
 * These allocate and release file read/write context information.
 */
int nfs_open(struct inode *inode, struct file *filp)
{
	struct nfs_open_context *ctx;
	struct rpc_cred *cred;

	cred = rpcauth_lookupcred(NFS_CLIENT(inode)->cl_auth, 0);
	if (IS_ERR(cred))
		return PTR_ERR(cred);
	ctx = alloc_nfs_open_context(filp->f_dentry, cred);
	put_rpccred(cred);
	if (ctx == NULL)
		return -ENOMEM;
	ctx->mode = filp->f_mode;
	nfs_file_set_open_context(filp, ctx);
	put_nfs_open_context(ctx);
	if ((filp->f_mode & FMODE_WRITE) != 0)
		nfs_begin_data_update(inode);
	return 0;
}

int nfs_release(struct inode *inode, struct file *filp)
{
	if ((filp->f_mode & FMODE_WRITE) != 0)
		nfs_end_data_update(inode);
	nfs_file_clear_open_context(filp);
	return 0;
}

/*
 * This function is called whenever some part of NFS notices that
 * the cached attributes have to be refreshed.
 */
int
__nfs_revalidate_inode(struct nfs_server *server, struct inode *inode)
{
	int		 status = -ESTALE;
	struct nfs_fattr fattr;
	struct nfs_inode *nfsi = NFS_I(inode);
	unsigned long verifier;
	unsigned long cache_validity;

	dfprintk(PAGECACHE, "NFS: revalidating (%s/%Ld)\n",
		inode->i_sb->s_id, (long long)NFS_FILEID(inode));

	lock_kernel();
	if (!inode || is_bad_inode(inode))
 		goto out_nowait;
	if (NFS_STALE(inode))
 		goto out_nowait;

	status = nfs_wait_on_inode(inode);
	if (status < 0)
		goto out;
	if (NFS_STALE(inode)) {
		status = -ESTALE;
		/* Do we trust the cached ESTALE? */
		if (NFS_ATTRTIMEO(inode) != 0) {
			if (nfsi->cache_validity & (NFS_INO_INVALID_ATTR|NFS_INO_INVALID_DATA|NFS_INO_INVALID_ATIME)) {
				/* no */
			} else
				goto out;
		}
	}

	/* Protect against RPC races by saving the change attribute */
	verifier = nfs_save_change_attribute(inode);
	status = NFS_PROTO(inode)->getattr(server, NFS_FH(inode), &fattr);
	if (status != 0) {
		dfprintk(PAGECACHE, "nfs_revalidate_inode: (%s/%Ld) getattr failed, error=%d\n",
			 inode->i_sb->s_id,
			 (long long)NFS_FILEID(inode), status);
		if (status == -ESTALE) {
			nfs_zap_caches(inode);
			if (!S_ISDIR(inode->i_mode))
				set_bit(NFS_INO_STALE, &NFS_FLAGS(inode));
		}
		goto out;
	}

	status = nfs_update_inode(inode, &fattr, verifier);
	if (status) {
		dfprintk(PAGECACHE, "nfs_revalidate_inode: (%s/%Ld) refresh failed, error=%d\n",
			 inode->i_sb->s_id,
			 (long long)NFS_FILEID(inode), status);
		goto out;
	}
	spin_lock(&inode->i_lock);
	cache_validity = nfsi->cache_validity;
	nfsi->cache_validity &= ~NFS_INO_REVAL_PAGECACHE;

	/*
	 * We may need to keep the attributes marked as invalid if
	 * we raced with nfs_end_attr_update().
	 */
	if (verifier == nfsi->cache_change_attribute)
		nfsi->cache_validity &= ~(NFS_INO_INVALID_ATTR|NFS_INO_INVALID_ATIME);
	spin_unlock(&inode->i_lock);

	nfs_revalidate_mapping(inode, inode->i_mapping);

	if (cache_validity & NFS_INO_INVALID_ACL)
		nfs_zap_acl_cache(inode);

	dfprintk(PAGECACHE, "NFS: (%s/%Ld) revalidation complete\n",
		inode->i_sb->s_id,
		(long long)NFS_FILEID(inode));

 out:
	nfs_wake_up_inode(inode);

 out_nowait:
	unlock_kernel();
	return status;
}

int nfs_attribute_timeout(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	if (nfs_have_delegation(inode, FMODE_READ))
		return 0;
	return time_after(jiffies, nfsi->read_cache_jiffies+nfsi->attrtimeo);
}

/**
 * nfs_revalidate_inode - Revalidate the inode attributes
 * @server - pointer to nfs_server struct
 * @inode - pointer to inode struct
 *
 * Updates inode attribute information by retrieving the data from the server.
 */
int nfs_revalidate_inode(struct nfs_server *server, struct inode *inode)
{
	if (!(NFS_I(inode)->cache_validity & (NFS_INO_INVALID_ATTR|NFS_INO_INVALID_DATA))
			&& !nfs_attribute_timeout(inode))
		return NFS_STALE(inode) ? -ESTALE : 0;
	return __nfs_revalidate_inode(server, inode);
}

/**
 * nfs_revalidate_mapping - Revalidate the pagecache
 * @inode - pointer to host inode
 * @mapping - pointer to mapping
 */
void nfs_revalidate_mapping(struct inode *inode, struct address_space *mapping)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	if (nfsi->cache_validity & NFS_INO_INVALID_DATA) {
		if (S_ISREG(inode->i_mode)) {
			if (filemap_fdatawrite(mapping) == 0)
				filemap_fdatawait(mapping);
			nfs_wb_all(inode);
		}
		invalidate_inode_pages2(mapping);

		spin_lock(&inode->i_lock);
		nfsi->cache_validity &= ~NFS_INO_INVALID_DATA;
		if (S_ISDIR(inode->i_mode)) {
			memset(nfsi->cookieverf, 0, sizeof(nfsi->cookieverf));
			/* This ensures we revalidate child dentries */
			nfsi->cache_change_attribute++;
		}
		spin_unlock(&inode->i_lock);

		dfprintk(PAGECACHE, "NFS: (%s/%Ld) data cache invalidated\n",
				inode->i_sb->s_id,
				(long long)NFS_FILEID(inode));
	}
}

/**
 * nfs_begin_data_update
 * @inode - pointer to inode
 * Declare that a set of operations will update file data on the server
 */
void nfs_begin_data_update(struct inode *inode)
{
	atomic_inc(&NFS_I(inode)->data_updates);
}

/**
 * nfs_end_data_update
 * @inode - pointer to inode
 * Declare end of the operations that will update file data
 * This will mark the inode as immediately needing revalidation
 * of its attribute cache.
 */
void nfs_end_data_update(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	if (!nfs_have_delegation(inode, FMODE_READ)) {
		/* Mark the attribute cache for revalidation */
		spin_lock(&inode->i_lock);
		nfsi->cache_validity |= NFS_INO_INVALID_ATTR;
		/* Directories and symlinks: invalidate page cache too */
		if (S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode))
			nfsi->cache_validity |= NFS_INO_INVALID_DATA;
		spin_unlock(&inode->i_lock);
	}
	nfsi->cache_change_attribute ++;
	atomic_dec(&nfsi->data_updates);
}

/**
 * nfs_refresh_inode - verify consistency of the inode attribute cache
 * @inode - pointer to inode
 * @fattr - updated attributes
 *
 * Verifies the attribute cache. If we have just changed the attributes,
 * so that fattr carries weak cache consistency data, then it may
 * also update the ctime/mtime/change_attribute.
 */
int nfs_refresh_inode(struct inode *inode, struct nfs_fattr *fattr)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	loff_t cur_size, new_isize;
	int data_unstable;

	spin_lock(&inode->i_lock);

	/* Are we in the process of updating data on the server? */
	data_unstable = nfs_caches_unstable(inode);

	if (fattr->valid & NFS_ATTR_FATTR_V4) {
		if ((fattr->valid & NFS_ATTR_PRE_CHANGE) != 0
				&& nfsi->change_attr == fattr->pre_change_attr)
			nfsi->change_attr = fattr->change_attr;
		if (nfsi->change_attr != fattr->change_attr) {
			nfsi->cache_validity |= NFS_INO_INVALID_ATTR;
			if (!data_unstable)
				nfsi->cache_validity |= NFS_INO_REVAL_PAGECACHE;
		}
	}

	if ((fattr->valid & NFS_ATTR_FATTR) == 0) {
		spin_unlock(&inode->i_lock);
		return 0;
	}

	/* Has the inode gone and changed behind our back? */
	if (nfsi->fileid != fattr->fileid
			|| (inode->i_mode & S_IFMT) != (fattr->mode & S_IFMT)) {
		spin_unlock(&inode->i_lock);
		return -EIO;
	}

	cur_size = i_size_read(inode);
 	new_isize = nfs_size_to_loff_t(fattr->size);

	/* If we have atomic WCC data, we may update some attributes */
	if ((fattr->valid & NFS_ATTR_WCC) != 0) {
		if (timespec_equal(&inode->i_ctime, &fattr->pre_ctime))
			memcpy(&inode->i_ctime, &fattr->ctime, sizeof(inode->i_ctime));
		if (timespec_equal(&inode->i_mtime, &fattr->pre_mtime))
			memcpy(&inode->i_mtime, &fattr->mtime, sizeof(inode->i_mtime));
	}

	/* Verify a few of the more important attributes */
	if (!timespec_equal(&inode->i_mtime, &fattr->mtime)) {
		nfsi->cache_validity |= NFS_INO_INVALID_ATTR;
		if (!data_unstable)
			nfsi->cache_validity |= NFS_INO_REVAL_PAGECACHE;
	}
	if (cur_size != new_isize) {
		nfsi->cache_validity |= NFS_INO_INVALID_ATTR;
		if (nfsi->npages == 0)
			nfsi->cache_validity |= NFS_INO_REVAL_PAGECACHE;
	}

	/* Have any file permissions changed? */
	if ((inode->i_mode & S_IALLUGO) != (fattr->mode & S_IALLUGO)
			|| inode->i_uid != fattr->uid
			|| inode->i_gid != fattr->gid)
		nfsi->cache_validity |= NFS_INO_INVALID_ATTR | NFS_INO_INVALID_ACCESS | NFS_INO_INVALID_ACL;

	/* Has the link count changed? */
	if (inode->i_nlink != fattr->nlink)
		nfsi->cache_validity |= NFS_INO_INVALID_ATTR;

	if (!timespec_equal(&inode->i_atime, &fattr->atime))
		nfsi->cache_validity |= NFS_INO_INVALID_ATIME;

	nfsi->read_cache_jiffies = fattr->timestamp;
	spin_unlock(&inode->i_lock);
	return 0;
}

/*
 * Many nfs protocol calls return the new file attributes after
 * an operation.  Here we update the inode to reflect the state
 * of the server's inode.
 *
 * This is a bit tricky because we have to make sure all dirty pages
 * have been sent off to the server before calling invalidate_inode_pages.
 * To make sure no other process adds more write requests while we try
 * our best to flush them, we make them sleep during the attribute refresh.
 *
 * A very similar scenario holds for the dir cache.
 */
static int nfs_update_inode(struct inode *inode, struct nfs_fattr *fattr, unsigned long verifier)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	loff_t cur_isize, new_isize;
	unsigned int	invalid = 0;
	int data_unstable;

	dfprintk(VFS, "NFS: %s(%s/%ld ct=%d info=0x%x)\n",
			__FUNCTION__, inode->i_sb->s_id, inode->i_ino,
			atomic_read(&inode->i_count), fattr->valid);

	if ((fattr->valid & NFS_ATTR_FATTR) == 0)
		return 0;

	if (nfsi->fileid != fattr->fileid) {
		printk(KERN_ERR "%s: inode number mismatch\n"
		       "expected (%s/0x%Lx), got (%s/0x%Lx)\n",
		       __FUNCTION__,
		       inode->i_sb->s_id, (long long)nfsi->fileid,
		       inode->i_sb->s_id, (long long)fattr->fileid);
		goto out_err;
	}

	spin_lock(&inode->i_lock);

	/*
	 * Make sure the inode's type hasn't changed.
	 */
	if ((inode->i_mode & S_IFMT) != (fattr->mode & S_IFMT)) {
		spin_unlock(&inode->i_lock);
		goto out_changed;
	}

	/*
	 * Update the read time so we don't revalidate too often.
	 */
	nfsi->read_cache_jiffies = fattr->timestamp;

	/* Are we racing with known updates of the metadata on the server? */
	data_unstable = ! (nfs_verify_change_attribute(inode, verifier) ||
		(nfsi->cache_validity & NFS_INO_REVAL_PAGECACHE));

	/* Check if our cached file size is stale */
 	new_isize = nfs_size_to_loff_t(fattr->size);
	cur_isize = i_size_read(inode);
	if (new_isize != cur_isize) {
		/* Do we perhaps have any outstanding writes? */
		if (nfsi->npages == 0) {
			/* No, but did we race with nfs_end_data_update()? */
			if (verifier  ==  nfsi->cache_change_attribute) {
				inode->i_size = new_isize;
				invalid |= NFS_INO_INVALID_DATA;
			}
			invalid |= NFS_INO_INVALID_ATTR;
		} else if (new_isize > cur_isize) {
			inode->i_size = new_isize;
			invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_DATA;
		}
		dprintk("NFS: isize change on server for file %s/%ld\n",
				inode->i_sb->s_id, inode->i_ino);
	}

	/* Check if the mtime agrees */
	if (!timespec_equal(&inode->i_mtime, &fattr->mtime)) {
		memcpy(&inode->i_mtime, &fattr->mtime, sizeof(inode->i_mtime));
		dprintk("NFS: mtime change on server for file %s/%ld\n",
				inode->i_sb->s_id, inode->i_ino);
		if (!data_unstable)
			invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_DATA;
	}

	if ((fattr->valid & NFS_ATTR_FATTR_V4)
	    && nfsi->change_attr != fattr->change_attr) {
		dprintk("NFS: change_attr change on server for file %s/%ld\n",
		       inode->i_sb->s_id, inode->i_ino);
		nfsi->change_attr = fattr->change_attr;
		if (!data_unstable)
			invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_DATA|NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL;
	}

	/* If ctime has changed we should definitely clear access+acl caches */
	if (!timespec_equal(&inode->i_ctime, &fattr->ctime)) {
		if (!data_unstable)
			invalid |= NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL;
		memcpy(&inode->i_ctime, &fattr->ctime, sizeof(inode->i_ctime));
	}
	memcpy(&inode->i_atime, &fattr->atime, sizeof(inode->i_atime));

	if ((inode->i_mode & S_IALLUGO) != (fattr->mode & S_IALLUGO) ||
	    inode->i_uid != fattr->uid ||
	    inode->i_gid != fattr->gid)
		invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL;

	inode->i_mode = fattr->mode;
	inode->i_nlink = fattr->nlink;
	inode->i_uid = fattr->uid;
	inode->i_gid = fattr->gid;

	if (fattr->valid & (NFS_ATTR_FATTR_V3 | NFS_ATTR_FATTR_V4)) {
		/*
		 * report the blocks in 512byte units
		 */
		inode->i_blocks = nfs_calc_block_size(fattr->du.nfs3.used);
		inode->i_blksize = inode->i_sb->s_blocksize;
 	} else {
 		inode->i_blocks = fattr->du.nfs2.blocks;
 		inode->i_blksize = fattr->du.nfs2.blocksize;
 	}

	/* Update attrtimeo value if we're out of the unstable period */
	if (invalid & NFS_INO_INVALID_ATTR) {
		nfsi->attrtimeo = NFS_MINATTRTIMEO(inode);
		nfsi->attrtimeo_timestamp = jiffies;
	} else if (time_after(jiffies, nfsi->attrtimeo_timestamp+nfsi->attrtimeo)) {
		if ((nfsi->attrtimeo <<= 1) > NFS_MAXATTRTIMEO(inode))
			nfsi->attrtimeo = NFS_MAXATTRTIMEO(inode);
		nfsi->attrtimeo_timestamp = jiffies;
	}
	/* Don't invalidate the data if we were to blame */
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)
				|| S_ISLNK(inode->i_mode)))
		invalid &= ~NFS_INO_INVALID_DATA;
	if (!nfs_have_delegation(inode, FMODE_READ))
		nfsi->cache_validity |= invalid;

	spin_unlock(&inode->i_lock);
	return 0;
 out_changed:
	/*
	 * Big trouble! The inode has become a different object.
	 */
#ifdef NFS_PARANOIA
	printk(KERN_DEBUG "%s: inode %ld mode changed, %07o to %07o\n",
			__FUNCTION__, inode->i_ino, inode->i_mode, fattr->mode);
#endif
	/*
	 * No need to worry about unhashing the dentry, as the
	 * lookup validation will know that the inode is bad.
	 * (But we fall through to invalidate the caches.)
	 */
	nfs_invalidate_inode(inode);
 out_err:
	set_bit(NFS_INO_STALE, &NFS_FLAGS(inode));
	return -ESTALE;
}

/*
 * File system information
 */

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

static struct super_block *nfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data)
{
	int error;
	struct nfs_server *server = NULL;
	struct super_block *s;
	struct nfs_fh *root;
	struct nfs_mount_data *data = raw_data;

	s = ERR_PTR(-EINVAL);
	if (data == NULL) {
		dprintk("%s: missing data argument\n", __FUNCTION__);
		goto out_err;
	}
	if (data->version <= 0 || data->version > NFS_MOUNT_VERSION) {
		dprintk("%s: bad mount version\n", __FUNCTION__);
		goto out_err;
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
				goto out_err;
			}
			data->root.size = NFS2_FHSIZE;
			memcpy(data->root.data, data->old_root.data, NFS2_FHSIZE);
		case 4:
			if (data->flags & NFS_MOUNT_SECFLAVOUR) {
				dprintk("%s: mount structure version %d does not support strong security\n",
						__FUNCTION__,
						data->version);
				goto out_err;
			}
		case 5:
			memset(data->context, 0, sizeof(data->context));
	}
#ifndef CONFIG_NFS_V3
	/* If NFSv3 is not compiled in, return -EPROTONOSUPPORT */
	s = ERR_PTR(-EPROTONOSUPPORT);
	if (data->flags & NFS_MOUNT_VER3) {
		dprintk("%s: NFSv3 not compiled into kernel\n", __FUNCTION__);
		goto out_err;
	}
#endif /* CONFIG_NFS_V3 */

	s = ERR_PTR(-ENOMEM);
	server = kmalloc(sizeof(struct nfs_server), GFP_KERNEL);
	if (!server)
		goto out_err;
	memset(server, 0, sizeof(struct nfs_server));
	/* Zero out the NFS state stuff */
	init_nfsv4_state(server);
	server->client = server->client_sys = server->client_acl = ERR_PTR(-EINVAL);

	root = &server->fh;
	if (data->flags & NFS_MOUNT_VER3)
		root->size = data->root.size;
	else
		root->size = NFS2_FHSIZE;
	s = ERR_PTR(-EINVAL);
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
	s = ERR_PTR(rpciod_up());
	if (IS_ERR(s)) {
		dprintk("%s: couldn't start rpciod! Error = %ld\n",
				__FUNCTION__, PTR_ERR(s));
		goto out_err;
	}

	s = sget(fs_type, nfs_compare_super, nfs_set_super, server);
	if (IS_ERR(s) || s->s_root)
		goto out_rpciod_down;

	s->s_flags = flags;

	error = nfs_fill_super(s, data, flags & MS_VERBOSE ? 1 : 0);
	if (error) {
		up_write(&s->s_umount);
		deactivate_super(s);
		return ERR_PTR(error);
	}
	s->s_flags |= MS_ACTIVE;
	return s;
out_rpciod_down:
	rpciod_down();
out_err:
	kfree(server);
	return s;
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

	if (server->hostname != NULL)
		kfree(server->hostname);
	kfree(server);
}

static struct file_system_type nfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs",
	.get_sb		= nfs_get_sb,
	.kill_sb	= nfs_kill_super,
	.fs_flags	= FS_ODD_RENAME|FS_REVAL_DOT|FS_BINARY_MOUNTDATA,
};

#ifdef CONFIG_NFS_V4

static void nfs4_clear_inode(struct inode *);


static struct super_operations nfs4_sops = { 
	.alloc_inode	= nfs_alloc_inode,
	.destroy_inode	= nfs_destroy_inode,
	.write_inode	= nfs_write_inode,
	.delete_inode	= nfs_delete_inode,
	.statfs		= nfs_statfs,
	.clear_inode	= nfs4_clear_inode,
	.umount_begin	= nfs_umount_begin,
	.show_options	= nfs_show_options,
};

/*
 * Clean out any remaining NFSv4 state that might be left over due
 * to open() calls that passed nfs_atomic_lookup, but failed to call
 * nfs_open().
 */
static void nfs4_clear_inode(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	/* If we are holding a delegation, return it! */
	if (nfsi->delegation != NULL)
		nfs_inode_return_delegation(inode);
	/* First call standard NFS clear_inode() code */
	nfs_clear_inode(inode);
	/* Now clear out any remaining state */
	while (!list_empty(&nfsi->open_states)) {
		struct nfs4_state *state;
		
		state = list_entry(nfsi->open_states.next,
				struct nfs4_state,
				inode_states);
		dprintk("%s(%s/%Ld): found unclaimed NFSv4 state %p\n",
				__FUNCTION__,
				inode->i_sb->s_id,
				(long long)NFS_FILEID(inode),
				state);
		BUG_ON(atomic_read(&state->count) != 1);
		nfs4_close_state(state, state->state);
	}
}


static int nfs4_fill_super(struct super_block *sb, struct nfs4_mount_data *data, int silent)
{
	struct nfs_server *server;
	struct nfs4_client *clp = NULL;
	struct rpc_xprt *xprt = NULL;
	struct rpc_clnt *clnt = NULL;
	struct rpc_timeout timeparms;
	rpc_authflavor_t authflavour;
	int proto, err = -EIO;

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
	/* Initialize timeout values */

	timeparms.to_initval = data->timeo * HZ / 10;
	timeparms.to_retries = data->retrans;
	timeparms.to_exponential = 1;
	if (!timeparms.to_retries)
		timeparms.to_retries = 5;

	proto = data->proto;
	/* Which IP protocol do we use? */
	switch (proto) {
	case IPPROTO_TCP:
		timeparms.to_maxval  = RPC_MAX_TCP_TIMEOUT;
		if (!timeparms.to_initval)
			timeparms.to_initval = 600 * HZ / 10;
		break;
	case IPPROTO_UDP:
		timeparms.to_maxval  = RPC_MAX_UDP_TIMEOUT;
		if (!timeparms.to_initval)
			timeparms.to_initval = 11 * HZ / 10;
		break;
	default:
		return -EINVAL;
	}

	clp = nfs4_get_client(&server->addr.sin_addr);
	if (!clp) {
		dprintk("%s: failed to create NFS4 client.\n", __FUNCTION__);
		return -EIO;
	}

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

	down_write(&clp->cl_sem);
	if (IS_ERR(clp->cl_rpcclient)) {
		xprt = xprt_create_proto(proto, &server->addr, &timeparms);
		if (IS_ERR(xprt)) {
			up_write(&clp->cl_sem);
			err = PTR_ERR(xprt);
			dprintk("%s: cannot create RPC transport. Error = %d\n",
					__FUNCTION__, err);
			goto out_fail;
		}
		clnt = rpc_create_client(xprt, server->hostname, &nfs_program,
				server->rpc_ops->version, authflavour);
		if (IS_ERR(clnt)) {
			up_write(&clp->cl_sem);
			err = PTR_ERR(clnt);
			dprintk("%s: cannot create RPC client. Error = %d\n",
					__FUNCTION__, err);
			goto out_fail;
		}
		clnt->cl_intr     = 1;
		clnt->cl_softrtry = 1;
		clnt->cl_chatty   = 1;
		clp->cl_rpcclient = clnt;
		clp->cl_cred = rpcauth_lookupcred(clnt->cl_auth, 0);
		if (IS_ERR(clp->cl_cred)) {
			up_write(&clp->cl_sem);
			err = PTR_ERR(clp->cl_cred);
			clp->cl_cred = NULL;
			goto out_fail;
		}
		memcpy(clp->cl_ipaddr, server->ip_addr, sizeof(clp->cl_ipaddr));
		nfs_idmap_new(clp);
	}
	if (list_empty(&clp->cl_superblocks)) {
		err = nfs4_init_client(clp);
		if (err != 0) {
			up_write(&clp->cl_sem);
			goto out_fail;
		}
	}
	list_add_tail(&server->nfs4_siblings, &clp->cl_superblocks);
	clnt = rpc_clone_client(clp->cl_rpcclient);
	if (!IS_ERR(clnt))
			server->nfs4_state = clp;
	up_write(&clp->cl_sem);
	clp = NULL;

	if (IS_ERR(clnt)) {
		err = PTR_ERR(clnt);
		dprintk("%s: cannot create RPC client. Error = %d\n",
				__FUNCTION__, err);
		return err;
	}

	server->client    = clnt;

	if (server->nfs4_state->cl_idmap == NULL) {
		dprintk("%s: failed to create idmapper.\n", __FUNCTION__);
		return -ENOMEM;
	}

	if (clnt->cl_auth->au_flavor != authflavour) {
		struct rpc_auth *auth;

		auth = rpcauth_create(authflavour, clnt);
		if (IS_ERR(auth)) {
			dprintk("%s: couldn't create credcache!\n", __FUNCTION__);
			return PTR_ERR(auth);
		}
	}

	sb->s_time_gran = 1;

	sb->s_op = &nfs4_sops;
	err = nfs_sb_init(sb, authflavour);
	if (err == 0)
		return 0;
out_fail:
	if (clp)
		nfs4_put_client(clp);
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
		if (p != NULL)
			kfree(p);
		return ERR_PTR(-EFAULT);
	}
	dst[maxlen] = '\0';
	return dst;
}

static struct super_block *nfs4_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data)
{
	int error;
	struct nfs_server *server;
	struct super_block *s;
	struct nfs4_mount_data *data = raw_data;
	void *p;

	if (data == NULL) {
		dprintk("%s: missing data argument\n", __FUNCTION__);
		return ERR_PTR(-EINVAL);
	}
	if (data->version <= 0 || data->version > NFS4_MOUNT_VERSION) {
		dprintk("%s: bad mount version\n", __FUNCTION__);
		return ERR_PTR(-EINVAL);
	}

	server = kmalloc(sizeof(struct nfs_server), GFP_KERNEL);
	if (!server)
		return ERR_PTR(-ENOMEM);
	memset(server, 0, sizeof(struct nfs_server));
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
		s = ERR_PTR(-EINVAL);
		goto out_free;
	}
	if (copy_from_user(&server->addr, data->host_addr, sizeof(server->addr))) {
		s = ERR_PTR(-EFAULT);
		goto out_free;
	}
	if (server->addr.sin_family != AF_INET ||
	    server->addr.sin_addr.s_addr == INADDR_ANY) {
		dprintk("%s: mount program didn't pass remote IP address!\n",
				__FUNCTION__);
		s = ERR_PTR(-EINVAL);
		goto out_free;
	}

	/* Fire up rpciod if not yet running */
	s = ERR_PTR(rpciod_up());
	if (IS_ERR(s)) {
		dprintk("%s: couldn't start rpciod! Error = %ld\n",
				__FUNCTION__, PTR_ERR(s));
		goto out_free;
	}

	s = sget(fs_type, nfs4_compare_super, nfs_set_super, server);

	if (IS_ERR(s) || s->s_root)
		goto out_free;

	s->s_flags = flags;

	error = nfs4_fill_super(s, data, flags & MS_VERBOSE ? 1 : 0);
	if (error) {
		up_write(&s->s_umount);
		deactivate_super(s);
		return ERR_PTR(error);
	}
	s->s_flags |= MS_ACTIVE;
	return s;
out_err:
	s = (struct super_block *)p;
out_free:
	if (server->mnt_path)
		kfree(server->mnt_path);
	if (server->hostname)
		kfree(server->hostname);
	kfree(server);
	return s;
}

static void nfs4_kill_super(struct super_block *sb)
{
	struct nfs_server *server = NFS_SB(sb);

	nfs_return_all_delegations(sb);
	kill_anon_super(sb);

	nfs4_renewd_prepare_shutdown(server);

	if (server->client != NULL && !IS_ERR(server->client))
		rpc_shutdown_client(server->client);
	rpciod_down();		/* release rpciod */

	destroy_nfsv4_state(server);

	if (server->hostname != NULL)
		kfree(server->hostname);
	kfree(server);
}

static struct file_system_type nfs4_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs4",
	.get_sb		= nfs4_get_sb,
	.kill_sb	= nfs4_kill_super,
	.fs_flags	= FS_ODD_RENAME|FS_REVAL_DOT|FS_BINARY_MOUNTDATA,
};

#define nfs4_init_once(nfsi) \
	do { \
		INIT_LIST_HEAD(&(nfsi)->open_states); \
		nfsi->delegation = NULL; \
		nfsi->delegation_state = 0; \
		init_rwsem(&nfsi->rwsem); \
	} while(0)
#define register_nfs4fs() register_filesystem(&nfs4_fs_type)
#define unregister_nfs4fs() unregister_filesystem(&nfs4_fs_type)
#else
#define nfs4_init_once(nfsi) \
	do { } while (0)
#define register_nfs4fs() (0)
#define unregister_nfs4fs()
#endif

extern int nfs_init_nfspagecache(void);
extern void nfs_destroy_nfspagecache(void);
extern int nfs_init_readpagecache(void);
extern void nfs_destroy_readpagecache(void);
extern int nfs_init_writepagecache(void);
extern void nfs_destroy_writepagecache(void);
#ifdef CONFIG_NFS_DIRECTIO
extern int nfs_init_directcache(void);
extern void nfs_destroy_directcache(void);
#endif

static kmem_cache_t * nfs_inode_cachep;

static struct inode *nfs_alloc_inode(struct super_block *sb)
{
	struct nfs_inode *nfsi;
	nfsi = (struct nfs_inode *)kmem_cache_alloc(nfs_inode_cachep, SLAB_KERNEL);
	if (!nfsi)
		return NULL;
	nfsi->flags = 0UL;
	nfsi->cache_validity = 0UL;
#ifdef CONFIG_NFS_V3_ACL
	nfsi->acl_access = ERR_PTR(-EAGAIN);
	nfsi->acl_default = ERR_PTR(-EAGAIN);
#endif
#ifdef CONFIG_NFS_V4
	nfsi->nfs4_acl = NULL;
#endif /* CONFIG_NFS_V4 */
	return &nfsi->vfs_inode;
}

static void nfs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(nfs_inode_cachep, NFS_I(inode));
}

static void init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct nfs_inode *nfsi = (struct nfs_inode *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		inode_init_once(&nfsi->vfs_inode);
		spin_lock_init(&nfsi->req_lock);
		INIT_LIST_HEAD(&nfsi->dirty);
		INIT_LIST_HEAD(&nfsi->commit);
		INIT_LIST_HEAD(&nfsi->open_files);
		INIT_RADIX_TREE(&nfsi->nfs_page_tree, GFP_ATOMIC);
		atomic_set(&nfsi->data_updates, 0);
		nfsi->ndirty = 0;
		nfsi->ncommit = 0;
		nfsi->npages = 0;
		nfs4_init_once(nfsi);
	}
}
 
static int nfs_init_inodecache(void)
{
	nfs_inode_cachep = kmem_cache_create("nfs_inode_cache",
					     sizeof(struct nfs_inode),
					     0, SLAB_RECLAIM_ACCOUNT,
					     init_once, NULL);
	if (nfs_inode_cachep == NULL)
		return -ENOMEM;

	return 0;
}

static void nfs_destroy_inodecache(void)
{
	if (kmem_cache_destroy(nfs_inode_cachep))
		printk(KERN_INFO "nfs_inode_cache: not all structures were freed\n");
}

/*
 * Initialize NFS
 */
static int __init init_nfs_fs(void)
{
	int err;

	err = nfs_init_nfspagecache();
	if (err)
		goto out4;

	err = nfs_init_inodecache();
	if (err)
		goto out3;

	err = nfs_init_readpagecache();
	if (err)
		goto out2;

	err = nfs_init_writepagecache();
	if (err)
		goto out1;

#ifdef CONFIG_NFS_DIRECTIO
	err = nfs_init_directcache();
	if (err)
		goto out0;
#endif

#ifdef CONFIG_PROC_FS
	rpc_proc_register(&nfs_rpcstat);
#endif
        err = register_filesystem(&nfs_fs_type);
	if (err)
		goto out;
	if ((err = register_nfs4fs()) != 0)
		goto out;
	return 0;
out:
#ifdef CONFIG_PROC_FS
	rpc_proc_unregister("nfs");
#endif
	nfs_destroy_writepagecache();
#ifdef CONFIG_NFS_DIRECTIO
out0:
	nfs_destroy_directcache();
#endif
out1:
	nfs_destroy_readpagecache();
out2:
	nfs_destroy_inodecache();
out3:
	nfs_destroy_nfspagecache();
out4:
	return err;
}

static void __exit exit_nfs_fs(void)
{
#ifdef CONFIG_NFS_DIRECTIO
	nfs_destroy_directcache();
#endif
	nfs_destroy_writepagecache();
	nfs_destroy_readpagecache();
	nfs_destroy_inodecache();
	nfs_destroy_nfspagecache();
#ifdef CONFIG_PROC_FS
	rpc_proc_unregister("nfs");
#endif
	unregister_filesystem(&nfs_fs_type);
	unregister_nfs4fs();
}

/* Not quite true; I just maintain it */
MODULE_AUTHOR("Olaf Kirch <okir@monad.swb.de>");
MODULE_LICENSE("GPL");

module_init(init_nfs_fs)
module_exit(exit_nfs_fs)
