
#include <linux/ceph/ceph_debug.h>

#include <linux/backing-dev.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/inet.h>
#include <linux/in6.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/parser.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/string.h>

#include "super.h"
#include "mds_client.h"

#include <linux/ceph/decode.h>
#include <linux/ceph/mon_client.h>
#include <linux/ceph/auth.h>
#include <linux/ceph/debugfs.h>

/*
 * Ceph superblock operations
 *
 * Handle the basics of mounting, unmounting.
 */

/*
 * super ops
 */
static void ceph_put_super(struct super_block *s)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(s);

	dout("put_super\n");
	ceph_mdsc_close_sessions(fsc->mdsc);

	/*
	 * ensure we release the bdi before put_anon_super releases
	 * the device name.
	 */
	if (s->s_bdi == &fsc->backing_dev_info) {
		bdi_unregister(&fsc->backing_dev_info);
		s->s_bdi = NULL;
	}

	return;
}

static int ceph_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct ceph_fs_client *fsc = ceph_inode_to_client(dentry->d_inode);
	struct ceph_monmap *monmap = fsc->client->monc.monmap;
	struct ceph_statfs st;
	u64 fsid;
	int err;

	dout("statfs\n");
	err = ceph_monc_do_statfs(&fsc->client->monc, &st);
	if (err < 0)
		return err;

	/* fill in kstatfs */
	buf->f_type = CEPH_SUPER_MAGIC;  /* ?? */

	/*
	 * express utilization in terms of large blocks to avoid
	 * overflow on 32-bit machines.
	 */
	buf->f_bsize = 1 << CEPH_BLOCK_SHIFT;
	buf->f_blocks = le64_to_cpu(st.kb) >> (CEPH_BLOCK_SHIFT-10);
	buf->f_bfree = le64_to_cpu(st.kb_avail) >> (CEPH_BLOCK_SHIFT-10);
	buf->f_bavail = le64_to_cpu(st.kb_avail) >> (CEPH_BLOCK_SHIFT-10);

	buf->f_files = le64_to_cpu(st.num_objects);
	buf->f_ffree = -1;
	buf->f_namelen = NAME_MAX;
	buf->f_frsize = PAGE_CACHE_SIZE;

	/* leave fsid little-endian, regardless of host endianness */
	fsid = *(u64 *)(&monmap->fsid) ^ *((u64 *)&monmap->fsid + 1);
	buf->f_fsid.val[0] = fsid & 0xffffffff;
	buf->f_fsid.val[1] = fsid >> 32;

	return 0;
}


static int ceph_sync_fs(struct super_block *sb, int wait)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(sb);

	if (!wait) {
		dout("sync_fs (non-blocking)\n");
		ceph_flush_dirty_caps(fsc->mdsc);
		dout("sync_fs (non-blocking) done\n");
		return 0;
	}

	dout("sync_fs (blocking)\n");
	ceph_osdc_sync(&fsc->client->osdc);
	ceph_mdsc_sync(fsc->mdsc);
	dout("sync_fs (blocking) done\n");
	return 0;
}

/*
 * mount options
 */
enum {
	Opt_wsize,
	Opt_rsize,
	Opt_rasize,
	Opt_caps_wanted_delay_min,
	Opt_caps_wanted_delay_max,
	Opt_cap_release_safety,
	Opt_readdir_max_entries,
	Opt_readdir_max_bytes,
	Opt_congestion_kb,
	Opt_last_int,
	/* int args above */
	Opt_snapdirname,
	Opt_last_string,
	/* string args above */
	Opt_dirstat,
	Opt_nodirstat,
	Opt_rbytes,
	Opt_norbytes,
	Opt_asyncreaddir,
	Opt_noasyncreaddir,
	Opt_dcache,
	Opt_nodcache,
	Opt_ino32,
	Opt_noino32,
};

static match_table_t fsopt_tokens = {
	{Opt_wsize, "wsize=%d"},
	{Opt_rsize, "rsize=%d"},
	{Opt_rasize, "rasize=%d"},
	{Opt_caps_wanted_delay_min, "caps_wanted_delay_min=%d"},
	{Opt_caps_wanted_delay_max, "caps_wanted_delay_max=%d"},
	{Opt_cap_release_safety, "cap_release_safety=%d"},
	{Opt_readdir_max_entries, "readdir_max_entries=%d"},
	{Opt_readdir_max_bytes, "readdir_max_bytes=%d"},
	{Opt_congestion_kb, "write_congestion_kb=%d"},
	/* int args above */
	{Opt_snapdirname, "snapdirname=%s"},
	/* string args above */
	{Opt_dirstat, "dirstat"},
	{Opt_nodirstat, "nodirstat"},
	{Opt_rbytes, "rbytes"},
	{Opt_norbytes, "norbytes"},
	{Opt_asyncreaddir, "asyncreaddir"},
	{Opt_noasyncreaddir, "noasyncreaddir"},
	{Opt_dcache, "dcache"},
	{Opt_nodcache, "nodcache"},
	{Opt_ino32, "ino32"},
	{Opt_noino32, "noino32"},
	{-1, NULL}
};

static int parse_fsopt_token(char *c, void *private)
{
	struct ceph_mount_options *fsopt = private;
	substring_t argstr[MAX_OPT_ARGS];
	int token, intval, ret;

	token = match_token((char *)c, fsopt_tokens, argstr);
	if (token < 0)
		return -EINVAL;

	if (token < Opt_last_int) {
		ret = match_int(&argstr[0], &intval);
		if (ret < 0) {
			pr_err("bad mount option arg (not int) "
			       "at '%s'\n", c);
			return ret;
		}
		dout("got int token %d val %d\n", token, intval);
	} else if (token > Opt_last_int && token < Opt_last_string) {
		dout("got string token %d val %s\n", token,
		     argstr[0].from);
	} else {
		dout("got token %d\n", token);
	}

	switch (token) {
	case Opt_snapdirname:
		kfree(fsopt->snapdir_name);
		fsopt->snapdir_name = kstrndup(argstr[0].from,
					       argstr[0].to-argstr[0].from,
					       GFP_KERNEL);
		if (!fsopt->snapdir_name)
			return -ENOMEM;
		break;

		/* misc */
	case Opt_wsize:
		fsopt->wsize = intval;
		break;
	case Opt_rsize:
		fsopt->rsize = intval;
		break;
	case Opt_rasize:
		fsopt->rasize = intval;
		break;
	case Opt_caps_wanted_delay_min:
		fsopt->caps_wanted_delay_min = intval;
		break;
	case Opt_caps_wanted_delay_max:
		fsopt->caps_wanted_delay_max = intval;
		break;
	case Opt_readdir_max_entries:
		fsopt->max_readdir = intval;
		break;
	case Opt_readdir_max_bytes:
		fsopt->max_readdir_bytes = intval;
		break;
	case Opt_congestion_kb:
		fsopt->congestion_kb = intval;
		break;
	case Opt_dirstat:
		fsopt->flags |= CEPH_MOUNT_OPT_DIRSTAT;
		break;
	case Opt_nodirstat:
		fsopt->flags &= ~CEPH_MOUNT_OPT_DIRSTAT;
		break;
	case Opt_rbytes:
		fsopt->flags |= CEPH_MOUNT_OPT_RBYTES;
		break;
	case Opt_norbytes:
		fsopt->flags &= ~CEPH_MOUNT_OPT_RBYTES;
		break;
	case Opt_asyncreaddir:
		fsopt->flags &= ~CEPH_MOUNT_OPT_NOASYNCREADDIR;
		break;
	case Opt_noasyncreaddir:
		fsopt->flags |= CEPH_MOUNT_OPT_NOASYNCREADDIR;
		break;
	case Opt_dcache:
		fsopt->flags |= CEPH_MOUNT_OPT_DCACHE;
		break;
	case Opt_nodcache:
		fsopt->flags &= ~CEPH_MOUNT_OPT_DCACHE;
		break;
	case Opt_ino32:
		fsopt->flags |= CEPH_MOUNT_OPT_INO32;
		break;
	case Opt_noino32:
		fsopt->flags &= ~CEPH_MOUNT_OPT_INO32;
		break;
	default:
		BUG_ON(token);
	}
	return 0;
}

static void destroy_mount_options(struct ceph_mount_options *args)
{
	dout("destroy_mount_options %p\n", args);
	kfree(args->snapdir_name);
	kfree(args);
}

static int strcmp_null(const char *s1, const char *s2)
{
	if (!s1 && !s2)
		return 0;
	if (s1 && !s2)
		return -1;
	if (!s1 && s2)
		return 1;
	return strcmp(s1, s2);
}

static int compare_mount_options(struct ceph_mount_options *new_fsopt,
				 struct ceph_options *new_opt,
				 struct ceph_fs_client *fsc)
{
	struct ceph_mount_options *fsopt1 = new_fsopt;
	struct ceph_mount_options *fsopt2 = fsc->mount_options;
	int ofs = offsetof(struct ceph_mount_options, snapdir_name);
	int ret;

	ret = memcmp(fsopt1, fsopt2, ofs);
	if (ret)
		return ret;

	ret = strcmp_null(fsopt1->snapdir_name, fsopt2->snapdir_name);
	if (ret)
		return ret;

	return ceph_compare_options(new_opt, fsc->client);
}

static int parse_mount_options(struct ceph_mount_options **pfsopt,
			       struct ceph_options **popt,
			       int flags, char *options,
			       const char *dev_name,
			       const char **path)
{
	struct ceph_mount_options *fsopt;
	const char *dev_name_end;
	int err = -ENOMEM;

	fsopt = kzalloc(sizeof(*fsopt), GFP_KERNEL);
	if (!fsopt)
		return -ENOMEM;

	dout("parse_mount_options %p, dev_name '%s'\n", fsopt, dev_name);

	fsopt->sb_flags = flags;
	fsopt->flags = CEPH_MOUNT_OPT_DEFAULT;

	fsopt->rsize = CEPH_RSIZE_DEFAULT;
	fsopt->rasize = CEPH_RASIZE_DEFAULT;
	fsopt->snapdir_name = kstrdup(CEPH_SNAPDIRNAME_DEFAULT, GFP_KERNEL);
	fsopt->caps_wanted_delay_min = CEPH_CAPS_WANTED_DELAY_MIN_DEFAULT;
	fsopt->caps_wanted_delay_max = CEPH_CAPS_WANTED_DELAY_MAX_DEFAULT;
	fsopt->cap_release_safety = CEPH_CAP_RELEASE_SAFETY_DEFAULT;
	fsopt->max_readdir = CEPH_MAX_READDIR_DEFAULT;
	fsopt->max_readdir_bytes = CEPH_MAX_READDIR_BYTES_DEFAULT;
	fsopt->congestion_kb = default_congestion_kb();

	/* ip1[:port1][,ip2[:port2]...]:/subdir/in/fs */
	err = -EINVAL;
	if (!dev_name)
		goto out;
	*path = strstr(dev_name, ":/");
	if (*path == NULL) {
		pr_err("device name is missing path (no :/ in %s)\n",
				dev_name);
		goto out;
	}
	dev_name_end = *path;
	dout("device name '%.*s'\n", (int)(dev_name_end - dev_name), dev_name);

	/* path on server */
	*path += 2;
	dout("server path '%s'\n", *path);

	*popt = ceph_parse_options(options, dev_name, dev_name_end,
				 parse_fsopt_token, (void *)fsopt);
	if (IS_ERR(*popt)) {
		err = PTR_ERR(*popt);
		goto out;
	}

	/* success */
	*pfsopt = fsopt;
	return 0;

out:
	destroy_mount_options(fsopt);
	return err;
}

/**
 * ceph_show_options - Show mount options in /proc/mounts
 * @m: seq_file to write to
 * @root: root of that (sub)tree
 */
static int ceph_show_options(struct seq_file *m, struct dentry *root)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(root->d_sb);
	struct ceph_mount_options *fsopt = fsc->mount_options;
	struct ceph_options *opt = fsc->client->options;

	if (opt->flags & CEPH_OPT_FSID)
		seq_printf(m, ",fsid=%pU", &opt->fsid);
	if (opt->flags & CEPH_OPT_NOSHARE)
		seq_puts(m, ",noshare");
	if (opt->flags & CEPH_OPT_NOCRC)
		seq_puts(m, ",nocrc");

	if (opt->name)
		seq_printf(m, ",name=%s", opt->name);
	if (opt->key)
		seq_puts(m, ",secret=<hidden>");

	if (opt->mount_timeout != CEPH_MOUNT_TIMEOUT_DEFAULT)
		seq_printf(m, ",mount_timeout=%d", opt->mount_timeout);
	if (opt->osd_idle_ttl != CEPH_OSD_IDLE_TTL_DEFAULT)
		seq_printf(m, ",osd_idle_ttl=%d", opt->osd_idle_ttl);
	if (opt->osd_timeout != CEPH_OSD_TIMEOUT_DEFAULT)
		seq_printf(m, ",osdtimeout=%d", opt->osd_timeout);
	if (opt->osd_keepalive_timeout != CEPH_OSD_KEEPALIVE_DEFAULT)
		seq_printf(m, ",osdkeepalivetimeout=%d",
			   opt->osd_keepalive_timeout);

	if (fsopt->flags & CEPH_MOUNT_OPT_DIRSTAT)
		seq_puts(m, ",dirstat");
	if ((fsopt->flags & CEPH_MOUNT_OPT_RBYTES) == 0)
		seq_puts(m, ",norbytes");
	if (fsopt->flags & CEPH_MOUNT_OPT_NOASYNCREADDIR)
		seq_puts(m, ",noasyncreaddir");
	if (fsopt->flags & CEPH_MOUNT_OPT_DCACHE)
		seq_puts(m, ",dcache");
	else
		seq_puts(m, ",nodcache");

	if (fsopt->wsize)
		seq_printf(m, ",wsize=%d", fsopt->wsize);
	if (fsopt->rsize != CEPH_RSIZE_DEFAULT)
		seq_printf(m, ",rsize=%d", fsopt->rsize);
	if (fsopt->rasize != CEPH_RASIZE_DEFAULT)
		seq_printf(m, ",rasize=%d", fsopt->rasize);
	if (fsopt->congestion_kb != default_congestion_kb())
		seq_printf(m, ",write_congestion_kb=%d", fsopt->congestion_kb);
	if (fsopt->caps_wanted_delay_min != CEPH_CAPS_WANTED_DELAY_MIN_DEFAULT)
		seq_printf(m, ",caps_wanted_delay_min=%d",
			 fsopt->caps_wanted_delay_min);
	if (fsopt->caps_wanted_delay_max != CEPH_CAPS_WANTED_DELAY_MAX_DEFAULT)
		seq_printf(m, ",caps_wanted_delay_max=%d",
			   fsopt->caps_wanted_delay_max);
	if (fsopt->cap_release_safety != CEPH_CAP_RELEASE_SAFETY_DEFAULT)
		seq_printf(m, ",cap_release_safety=%d",
			   fsopt->cap_release_safety);
	if (fsopt->max_readdir != CEPH_MAX_READDIR_DEFAULT)
		seq_printf(m, ",readdir_max_entries=%d", fsopt->max_readdir);
	if (fsopt->max_readdir_bytes != CEPH_MAX_READDIR_BYTES_DEFAULT)
		seq_printf(m, ",readdir_max_bytes=%d", fsopt->max_readdir_bytes);
	if (strcmp(fsopt->snapdir_name, CEPH_SNAPDIRNAME_DEFAULT))
		seq_printf(m, ",snapdirname=%s", fsopt->snapdir_name);
	return 0;
}

/*
 * handle any mon messages the standard library doesn't understand.
 * return error if we don't either.
 */
static int extra_mon_dispatch(struct ceph_client *client, struct ceph_msg *msg)
{
	struct ceph_fs_client *fsc = client->private;
	int type = le16_to_cpu(msg->hdr.type);

	switch (type) {
	case CEPH_MSG_MDS_MAP:
		ceph_mdsc_handle_map(fsc->mdsc, msg);
		return 0;

	default:
		return -1;
	}
}

/*
 * create a new fs client
 */
static struct ceph_fs_client *create_fs_client(struct ceph_mount_options *fsopt,
					struct ceph_options *opt)
{
	struct ceph_fs_client *fsc;
	const unsigned supported_features =
		CEPH_FEATURE_FLOCK |
		CEPH_FEATURE_DIRLAYOUTHASH;
	const unsigned required_features = 0;
	int err = -ENOMEM;

	fsc = kzalloc(sizeof(*fsc), GFP_KERNEL);
	if (!fsc)
		return ERR_PTR(-ENOMEM);

	fsc->client = ceph_create_client(opt, fsc, supported_features,
					 required_features);
	if (IS_ERR(fsc->client)) {
		err = PTR_ERR(fsc->client);
		goto fail;
	}
	fsc->client->extra_mon_dispatch = extra_mon_dispatch;
	fsc->client->monc.want_mdsmap = 1;

	fsc->mount_options = fsopt;

	fsc->sb = NULL;
	fsc->mount_state = CEPH_MOUNT_MOUNTING;

	atomic_long_set(&fsc->writeback_count, 0);

	err = bdi_init(&fsc->backing_dev_info);
	if (err < 0)
		goto fail_client;

	err = -ENOMEM;
	/*
	 * The number of concurrent works can be high but they don't need
	 * to be processed in parallel, limit concurrency.
	 */
	fsc->wb_wq = alloc_workqueue("ceph-writeback", 0, 1);
	if (fsc->wb_wq == NULL)
		goto fail_bdi;
	fsc->pg_inv_wq = alloc_workqueue("ceph-pg-invalid", 0, 1);
	if (fsc->pg_inv_wq == NULL)
		goto fail_wb_wq;
	fsc->trunc_wq = alloc_workqueue("ceph-trunc", 0, 1);
	if (fsc->trunc_wq == NULL)
		goto fail_pg_inv_wq;

	/* set up mempools */
	err = -ENOMEM;
	fsc->wb_pagevec_pool = mempool_create_kmalloc_pool(10,
			      fsc->mount_options->wsize >> PAGE_CACHE_SHIFT);
	if (!fsc->wb_pagevec_pool)
		goto fail_trunc_wq;

	/* caps */
	fsc->min_caps = fsopt->max_readdir;

	return fsc;

fail_trunc_wq:
	destroy_workqueue(fsc->trunc_wq);
fail_pg_inv_wq:
	destroy_workqueue(fsc->pg_inv_wq);
fail_wb_wq:
	destroy_workqueue(fsc->wb_wq);
fail_bdi:
	bdi_destroy(&fsc->backing_dev_info);
fail_client:
	ceph_destroy_client(fsc->client);
fail:
	kfree(fsc);
	return ERR_PTR(err);
}

static void destroy_fs_client(struct ceph_fs_client *fsc)
{
	dout("destroy_fs_client %p\n", fsc);

	destroy_workqueue(fsc->wb_wq);
	destroy_workqueue(fsc->pg_inv_wq);
	destroy_workqueue(fsc->trunc_wq);

	bdi_destroy(&fsc->backing_dev_info);

	mempool_destroy(fsc->wb_pagevec_pool);

	destroy_mount_options(fsc->mount_options);

	ceph_fs_debugfs_cleanup(fsc);

	ceph_destroy_client(fsc->client);

	kfree(fsc);
	dout("destroy_fs_client %p done\n", fsc);
}

/*
 * caches
 */
struct kmem_cache *ceph_inode_cachep;
struct kmem_cache *ceph_cap_cachep;
struct kmem_cache *ceph_dentry_cachep;
struct kmem_cache *ceph_file_cachep;

static void ceph_inode_init_once(void *foo)
{
	struct ceph_inode_info *ci = foo;
	inode_init_once(&ci->vfs_inode);
}

static int __init init_caches(void)
{
	ceph_inode_cachep = kmem_cache_create("ceph_inode_info",
				      sizeof(struct ceph_inode_info),
				      __alignof__(struct ceph_inode_info),
				      (SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD),
				      ceph_inode_init_once);
	if (ceph_inode_cachep == NULL)
		return -ENOMEM;

	ceph_cap_cachep = KMEM_CACHE(ceph_cap,
				     SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD);
	if (ceph_cap_cachep == NULL)
		goto bad_cap;

	ceph_dentry_cachep = KMEM_CACHE(ceph_dentry_info,
					SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD);
	if (ceph_dentry_cachep == NULL)
		goto bad_dentry;

	ceph_file_cachep = KMEM_CACHE(ceph_file_info,
				      SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD);
	if (ceph_file_cachep == NULL)
		goto bad_file;

	return 0;

bad_file:
	kmem_cache_destroy(ceph_dentry_cachep);
bad_dentry:
	kmem_cache_destroy(ceph_cap_cachep);
bad_cap:
	kmem_cache_destroy(ceph_inode_cachep);
	return -ENOMEM;
}

static void destroy_caches(void)
{
	kmem_cache_destroy(ceph_inode_cachep);
	kmem_cache_destroy(ceph_cap_cachep);
	kmem_cache_destroy(ceph_dentry_cachep);
	kmem_cache_destroy(ceph_file_cachep);
}


/*
 * ceph_umount_begin - initiate forced umount.  Tear down down the
 * mount, skipping steps that may hang while waiting for server(s).
 */
static void ceph_umount_begin(struct super_block *sb)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(sb);

	dout("ceph_umount_begin - starting forced umount\n");
	if (!fsc)
		return;
	fsc->mount_state = CEPH_MOUNT_SHUTDOWN;
	return;
}

static const struct super_operations ceph_super_ops = {
	.alloc_inode	= ceph_alloc_inode,
	.destroy_inode	= ceph_destroy_inode,
	.write_inode    = ceph_write_inode,
	.sync_fs        = ceph_sync_fs,
	.put_super	= ceph_put_super,
	.show_options   = ceph_show_options,
	.statfs		= ceph_statfs,
	.umount_begin   = ceph_umount_begin,
};

/*
 * Bootstrap mount by opening the root directory.  Note the mount
 * @started time from caller, and time out if this takes too long.
 */
static struct dentry *open_root_dentry(struct ceph_fs_client *fsc,
				       const char *path,
				       unsigned long started)
{
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req = NULL;
	int err;
	struct dentry *root;

	/* open dir */
	dout("open_root_inode opening '%s'\n", path);
	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_GETATTR, USE_ANY_MDS);
	if (IS_ERR(req))
		return ERR_CAST(req);
	req->r_path1 = kstrdup(path, GFP_NOFS);
	req->r_ino1.ino = CEPH_INO_ROOT;
	req->r_ino1.snap = CEPH_NOSNAP;
	req->r_started = started;
	req->r_timeout = fsc->client->options->mount_timeout * HZ;
	req->r_args.getattr.mask = cpu_to_le32(CEPH_STAT_CAP_INODE);
	req->r_num_caps = 2;
	err = ceph_mdsc_do_request(mdsc, NULL, req);
	if (err == 0) {
		struct inode *inode = req->r_target_inode;
		req->r_target_inode = NULL;
		dout("open_root_inode success\n");
		if (ceph_ino(inode) == CEPH_INO_ROOT &&
		    fsc->sb->s_root == NULL) {
			root = d_make_root(inode);
			if (!root) {
				root = ERR_PTR(-ENOMEM);
				goto out;
			}
		} else {
			root = d_obtain_alias(inode);
		}
		ceph_init_dentry(root);
		dout("open_root_inode success, root dentry is %p\n", root);
	} else {
		root = ERR_PTR(err);
	}
out:
	ceph_mdsc_put_request(req);
	return root;
}




/*
 * mount: join the ceph cluster, and open root directory.
 */
static struct dentry *ceph_real_mount(struct ceph_fs_client *fsc,
		      const char *path)
{
	int err;
	unsigned long started = jiffies;  /* note the start time */
	struct dentry *root;
	int first = 0;   /* first vfsmount for this super_block */

	dout("mount start\n");
	mutex_lock(&fsc->client->mount_mutex);

	err = __ceph_open_session(fsc->client, started);
	if (err < 0)
		goto out;

	dout("mount opening root\n");
	root = open_root_dentry(fsc, "", started);
	if (IS_ERR(root)) {
		err = PTR_ERR(root);
		goto out;
	}
	if (fsc->sb->s_root) {
		dput(root);
	} else {
		fsc->sb->s_root = root;
		first = 1;

		err = ceph_fs_debugfs_init(fsc);
		if (err < 0)
			goto fail;
	}

	if (path[0] == 0) {
		dget(root);
	} else {
		dout("mount opening base mountpoint\n");
		root = open_root_dentry(fsc, path, started);
		if (IS_ERR(root)) {
			err = PTR_ERR(root);
			goto fail;
		}
	}

	fsc->mount_state = CEPH_MOUNT_MOUNTED;
	dout("mount success\n");
	mutex_unlock(&fsc->client->mount_mutex);
	return root;

out:
	mutex_unlock(&fsc->client->mount_mutex);
	return ERR_PTR(err);

fail:
	if (first) {
		dput(fsc->sb->s_root);
		fsc->sb->s_root = NULL;
	}
	goto out;
}

static int ceph_set_super(struct super_block *s, void *data)
{
	struct ceph_fs_client *fsc = data;
	int ret;

	dout("set_super %p data %p\n", s, data);

	s->s_flags = fsc->mount_options->sb_flags;
	s->s_maxbytes = 1ULL << 40;  /* temp value until we get mdsmap */

	s->s_fs_info = fsc;
	fsc->sb = s;

	s->s_op = &ceph_super_ops;
	s->s_export_op = &ceph_export_ops;

	s->s_time_gran = 1000;  /* 1000 ns == 1 us */

	ret = set_anon_super(s, NULL);  /* what is that second arg for? */
	if (ret != 0)
		goto fail;

	return ret;

fail:
	s->s_fs_info = NULL;
	fsc->sb = NULL;
	return ret;
}

/*
 * share superblock if same fs AND options
 */
static int ceph_compare_super(struct super_block *sb, void *data)
{
	struct ceph_fs_client *new = data;
	struct ceph_mount_options *fsopt = new->mount_options;
	struct ceph_options *opt = new->client->options;
	struct ceph_fs_client *other = ceph_sb_to_client(sb);

	dout("ceph_compare_super %p\n", sb);

	if (compare_mount_options(fsopt, opt, other)) {
		dout("monitor(s)/mount options don't match\n");
		return 0;
	}
	if ((opt->flags & CEPH_OPT_FSID) &&
	    ceph_fsid_compare(&opt->fsid, &other->client->fsid)) {
		dout("fsid doesn't match\n");
		return 0;
	}
	if (fsopt->sb_flags != other->mount_options->sb_flags) {
		dout("flags differ\n");
		return 0;
	}
	return 1;
}

/*
 * construct our own bdi so we can control readahead, etc.
 */
static atomic_long_t bdi_seq = ATOMIC_LONG_INIT(0);

static int ceph_register_bdi(struct super_block *sb,
			     struct ceph_fs_client *fsc)
{
	int err;

	/* set ra_pages based on rasize mount option? */
	if (fsc->mount_options->rasize >= PAGE_CACHE_SIZE)
		fsc->backing_dev_info.ra_pages =
			(fsc->mount_options->rasize + PAGE_CACHE_SIZE - 1)
			>> PAGE_SHIFT;
	else
		fsc->backing_dev_info.ra_pages =
			default_backing_dev_info.ra_pages;

	err = bdi_register(&fsc->backing_dev_info, NULL, "ceph-%d",
			   atomic_long_inc_return(&bdi_seq));
	if (!err)
		sb->s_bdi = &fsc->backing_dev_info;
	return err;
}

static struct dentry *ceph_mount(struct file_system_type *fs_type,
		       int flags, const char *dev_name, void *data)
{
	struct super_block *sb;
	struct ceph_fs_client *fsc;
	struct dentry *res;
	int err;
	int (*compare_super)(struct super_block *, void *) = ceph_compare_super;
	const char *path = NULL;
	struct ceph_mount_options *fsopt = NULL;
	struct ceph_options *opt = NULL;

	dout("ceph_mount\n");
	err = parse_mount_options(&fsopt, &opt, flags, data, dev_name, &path);
	if (err < 0) {
		res = ERR_PTR(err);
		goto out_final;
	}

	/* create client (which we may/may not use) */
	fsc = create_fs_client(fsopt, opt);
	if (IS_ERR(fsc)) {
		res = ERR_CAST(fsc);
		destroy_mount_options(fsopt);
		ceph_destroy_options(opt);
		goto out_final;
	}

	err = ceph_mdsc_init(fsc);
	if (err < 0) {
		res = ERR_PTR(err);
		goto out;
	}

	if (ceph_test_opt(fsc->client, NOSHARE))
		compare_super = NULL;
	sb = sget(fs_type, compare_super, ceph_set_super, fsc);
	if (IS_ERR(sb)) {
		res = ERR_CAST(sb);
		goto out;
	}

	if (ceph_sb_to_client(sb) != fsc) {
		ceph_mdsc_destroy(fsc);
		destroy_fs_client(fsc);
		fsc = ceph_sb_to_client(sb);
		dout("get_sb got existing client %p\n", fsc);
	} else {
		dout("get_sb using new client %p\n", fsc);
		err = ceph_register_bdi(sb, fsc);
		if (err < 0) {
			res = ERR_PTR(err);
			goto out_splat;
		}
	}

	res = ceph_real_mount(fsc, path);
	if (IS_ERR(res))
		goto out_splat;
	dout("root %p inode %p ino %llx.%llx\n", res,
	     res->d_inode, ceph_vinop(res->d_inode));
	return res;

out_splat:
	ceph_mdsc_close_sessions(fsc->mdsc);
	deactivate_locked_super(sb);
	goto out_final;

out:
	ceph_mdsc_destroy(fsc);
	destroy_fs_client(fsc);
out_final:
	dout("ceph_mount fail %ld\n", PTR_ERR(res));
	return res;
}

static void ceph_kill_sb(struct super_block *s)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(s);
	dout("kill_sb %p\n", s);
	ceph_mdsc_pre_umount(fsc->mdsc);
	kill_anon_super(s);    /* will call put_super after sb is r/o */
	ceph_mdsc_destroy(fsc);
	destroy_fs_client(fsc);
}

static struct file_system_type ceph_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ceph",
	.mount		= ceph_mount,
	.kill_sb	= ceph_kill_sb,
	.fs_flags	= FS_RENAME_DOES_D_MOVE,
};

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

static int __init init_ceph(void)
{
	int ret = init_caches();
	if (ret)
		goto out;

	ceph_xattr_init();
	ret = register_filesystem(&ceph_fs_type);
	if (ret)
		goto out_icache;

	pr_info("loaded (mds proto %d)\n", CEPH_MDSC_PROTOCOL);

	return 0;

out_icache:
	ceph_xattr_exit();
	destroy_caches();
out:
	return ret;
}

static void __exit exit_ceph(void)
{
	dout("exit_ceph\n");
	unregister_filesystem(&ceph_fs_type);
	ceph_xattr_exit();
	destroy_caches();
}

module_init(init_ceph);
module_exit(exit_ceph);

MODULE_AUTHOR("Sage Weil <sage@newdream.net>");
MODULE_AUTHOR("Yehuda Sadeh <yehuda@hq.newdream.net>");
MODULE_AUTHOR("Patience Warnick <patience@newdream.net>");
MODULE_DESCRIPTION("Ceph filesystem for Linux");
MODULE_LICENSE("GPL");
