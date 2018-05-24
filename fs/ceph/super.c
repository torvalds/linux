
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
#include "cache.h"

#include <linux/ceph/ceph_features.h>
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
}

static int ceph_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct ceph_fs_client *fsc = ceph_inode_to_client(d_inode(dentry));
	struct ceph_monmap *monmap = fsc->client->monc.monmap;
	struct ceph_statfs st;
	u64 fsid;
	int err;
	u64 data_pool;

	if (fsc->mdsc->mdsmap->m_num_data_pg_pools == 1) {
		data_pool = fsc->mdsc->mdsmap->m_data_pg_pools[0];
	} else {
		data_pool = CEPH_NOPOOL;
	}

	dout("statfs\n");
	err = ceph_monc_do_statfs(&fsc->client->monc, data_pool, &st);
	if (err < 0)
		return err;

	/* fill in kstatfs */
	buf->f_type = CEPH_SUPER_MAGIC;  /* ?? */

	/*
	 * express utilization in terms of large blocks to avoid
	 * overflow on 32-bit machines.
	 *
	 * NOTE: for the time being, we make bsize == frsize to humor
	 * not-yet-ancient versions of glibc that are broken.
	 * Someday, we will probably want to report a real block
	 * size...  whatever that may mean for a network file system!
	 */
	buf->f_bsize = 1 << CEPH_BLOCK_SHIFT;
	buf->f_frsize = 1 << CEPH_BLOCK_SHIFT;

	/*
	 * By default use root quota for stats; fallback to overall filesystem
	 * usage if using 'noquotadf' mount option or if the root dir doesn't
	 * have max_bytes quota set.
	 */
	if (ceph_test_mount_opt(fsc, NOQUOTADF) ||
	    !ceph_quota_update_statfs(fsc, buf)) {
		buf->f_blocks = le64_to_cpu(st.kb) >> (CEPH_BLOCK_SHIFT-10);
		buf->f_bfree = le64_to_cpu(st.kb_avail) >> (CEPH_BLOCK_SHIFT-10);
		buf->f_bavail = le64_to_cpu(st.kb_avail) >> (CEPH_BLOCK_SHIFT-10);
	}

	buf->f_files = le64_to_cpu(st.num_objects);
	buf->f_ffree = -1;
	buf->f_namelen = NAME_MAX;

	/* Must convert the fsid, for consistent values across arches */
	fsid = le64_to_cpu(*(__le64 *)(&monmap->fsid)) ^
	       le64_to_cpu(*((__le64 *)&monmap->fsid + 1));
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
	Opt_readdir_max_entries,
	Opt_readdir_max_bytes,
	Opt_congestion_kb,
	Opt_last_int,
	/* int args above */
	Opt_snapdirname,
	Opt_mds_namespace,
	Opt_fscache_uniq,
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
	Opt_fscache,
	Opt_nofscache,
	Opt_poolperm,
	Opt_nopoolperm,
	Opt_require_active_mds,
	Opt_norequire_active_mds,
#ifdef CONFIG_CEPH_FS_POSIX_ACL
	Opt_acl,
#endif
	Opt_noacl,
	Opt_quotadf,
	Opt_noquotadf,
};

static match_table_t fsopt_tokens = {
	{Opt_wsize, "wsize=%d"},
	{Opt_rsize, "rsize=%d"},
	{Opt_rasize, "rasize=%d"},
	{Opt_caps_wanted_delay_min, "caps_wanted_delay_min=%d"},
	{Opt_caps_wanted_delay_max, "caps_wanted_delay_max=%d"},
	{Opt_readdir_max_entries, "readdir_max_entries=%d"},
	{Opt_readdir_max_bytes, "readdir_max_bytes=%d"},
	{Opt_congestion_kb, "write_congestion_kb=%d"},
	/* int args above */
	{Opt_snapdirname, "snapdirname=%s"},
	{Opt_mds_namespace, "mds_namespace=%s"},
	{Opt_fscache_uniq, "fsc=%s"},
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
	{Opt_fscache, "fsc"},
	{Opt_nofscache, "nofsc"},
	{Opt_poolperm, "poolperm"},
	{Opt_nopoolperm, "nopoolperm"},
	{Opt_require_active_mds, "require_active_mds"},
	{Opt_norequire_active_mds, "norequire_active_mds"},
#ifdef CONFIG_CEPH_FS_POSIX_ACL
	{Opt_acl, "acl"},
#endif
	{Opt_noacl, "noacl"},
	{Opt_quotadf, "quotadf"},
	{Opt_noquotadf, "noquotadf"},
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
	case Opt_mds_namespace:
		kfree(fsopt->mds_namespace);
		fsopt->mds_namespace = kstrndup(argstr[0].from,
						argstr[0].to-argstr[0].from,
						GFP_KERNEL);
		if (!fsopt->mds_namespace)
			return -ENOMEM;
		break;
	case Opt_fscache_uniq:
		kfree(fsopt->fscache_uniq);
		fsopt->fscache_uniq = kstrndup(argstr[0].from,
					       argstr[0].to-argstr[0].from,
					       GFP_KERNEL);
		if (!fsopt->fscache_uniq)
			return -ENOMEM;
		fsopt->flags |= CEPH_MOUNT_OPT_FSCACHE;
		break;
		/* misc */
	case Opt_wsize:
		if (intval < PAGE_SIZE || intval > CEPH_MAX_WRITE_SIZE)
			return -EINVAL;
		fsopt->wsize = ALIGN(intval, PAGE_SIZE);
		break;
	case Opt_rsize:
		if (intval < PAGE_SIZE || intval > CEPH_MAX_READ_SIZE)
			return -EINVAL;
		fsopt->rsize = ALIGN(intval, PAGE_SIZE);
		break;
	case Opt_rasize:
		if (intval < 0)
			return -EINVAL;
		fsopt->rasize = ALIGN(intval + PAGE_SIZE - 1, PAGE_SIZE);
		break;
	case Opt_caps_wanted_delay_min:
		if (intval < 1)
			return -EINVAL;
		fsopt->caps_wanted_delay_min = intval;
		break;
	case Opt_caps_wanted_delay_max:
		if (intval < 1)
			return -EINVAL;
		fsopt->caps_wanted_delay_max = intval;
		break;
	case Opt_readdir_max_entries:
		if (intval < 1)
			return -EINVAL;
		fsopt->max_readdir = intval;
		break;
	case Opt_readdir_max_bytes:
		if (intval < PAGE_SIZE && intval != 0)
			return -EINVAL;
		fsopt->max_readdir_bytes = intval;
		break;
	case Opt_congestion_kb:
		if (intval < 1024) /* at least 1M */
			return -EINVAL;
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
	case Opt_fscache:
		fsopt->flags |= CEPH_MOUNT_OPT_FSCACHE;
		kfree(fsopt->fscache_uniq);
		fsopt->fscache_uniq = NULL;
		break;
	case Opt_nofscache:
		fsopt->flags &= ~CEPH_MOUNT_OPT_FSCACHE;
		kfree(fsopt->fscache_uniq);
		fsopt->fscache_uniq = NULL;
		break;
	case Opt_poolperm:
		fsopt->flags &= ~CEPH_MOUNT_OPT_NOPOOLPERM;
		break;
	case Opt_nopoolperm:
		fsopt->flags |= CEPH_MOUNT_OPT_NOPOOLPERM;
		break;
	case Opt_require_active_mds:
		fsopt->flags &= ~CEPH_MOUNT_OPT_MOUNTWAIT;
		break;
	case Opt_norequire_active_mds:
		fsopt->flags |= CEPH_MOUNT_OPT_MOUNTWAIT;
		break;
	case Opt_quotadf:
		fsopt->flags &= ~CEPH_MOUNT_OPT_NOQUOTADF;
		break;
	case Opt_noquotadf:
		fsopt->flags |= CEPH_MOUNT_OPT_NOQUOTADF;
		break;
#ifdef CONFIG_CEPH_FS_POSIX_ACL
	case Opt_acl:
		fsopt->sb_flags |= SB_POSIXACL;
		break;
#endif
	case Opt_noacl:
		fsopt->sb_flags &= ~SB_POSIXACL;
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
	kfree(args->mds_namespace);
	kfree(args->server_path);
	kfree(args->fscache_uniq);
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
	ret = strcmp_null(fsopt1->mds_namespace, fsopt2->mds_namespace);
	if (ret)
		return ret;
	ret = strcmp_null(fsopt1->server_path, fsopt2->server_path);
	if (ret)
		return ret;
	ret = strcmp_null(fsopt1->fscache_uniq, fsopt2->fscache_uniq);
	if (ret)
		return ret;

	return ceph_compare_options(new_opt, fsc->client);
}

static int parse_mount_options(struct ceph_mount_options **pfsopt,
			       struct ceph_options **popt,
			       int flags, char *options,
			       const char *dev_name)
{
	struct ceph_mount_options *fsopt;
	const char *dev_name_end;
	int err;

	if (!dev_name || !*dev_name)
		return -EINVAL;

	fsopt = kzalloc(sizeof(*fsopt), GFP_KERNEL);
	if (!fsopt)
		return -ENOMEM;

	dout("parse_mount_options %p, dev_name '%s'\n", fsopt, dev_name);

	fsopt->sb_flags = flags;
	fsopt->flags = CEPH_MOUNT_OPT_DEFAULT;

	fsopt->wsize = CEPH_MAX_WRITE_SIZE;
	fsopt->rsize = CEPH_MAX_READ_SIZE;
	fsopt->rasize = CEPH_RASIZE_DEFAULT;
	fsopt->snapdir_name = kstrdup(CEPH_SNAPDIRNAME_DEFAULT, GFP_KERNEL);
	if (!fsopt->snapdir_name) {
		err = -ENOMEM;
		goto out;
	}

	fsopt->caps_wanted_delay_min = CEPH_CAPS_WANTED_DELAY_MIN_DEFAULT;
	fsopt->caps_wanted_delay_max = CEPH_CAPS_WANTED_DELAY_MAX_DEFAULT;
	fsopt->max_readdir = CEPH_MAX_READDIR_DEFAULT;
	fsopt->max_readdir_bytes = CEPH_MAX_READDIR_BYTES_DEFAULT;
	fsopt->congestion_kb = default_congestion_kb();

	/*
	 * Distinguish the server list from the path in "dev_name".
	 * Internally we do not include the leading '/' in the path.
	 *
	 * "dev_name" will look like:
	 *     <server_spec>[,<server_spec>...]:[<path>]
	 * where
	 *     <server_spec> is <ip>[:<port>]
	 *     <path> is optional, but if present must begin with '/'
	 */
	dev_name_end = strchr(dev_name, '/');
	if (dev_name_end) {
		if (strlen(dev_name_end) > 1) {
			fsopt->server_path = kstrdup(dev_name_end, GFP_KERNEL);
			if (!fsopt->server_path) {
				err = -ENOMEM;
				goto out;
			}
		}
	} else {
		dev_name_end = dev_name + strlen(dev_name);
	}
	err = -EINVAL;
	dev_name_end--;		/* back up to ':' separator */
	if (dev_name_end < dev_name || *dev_name_end != ':') {
		pr_err("device name is missing path (no : separator in %s)\n",
				dev_name);
		goto out;
	}
	dout("device name '%.*s'\n", (int)(dev_name_end - dev_name), dev_name);
	if (fsopt->server_path)
		dout("server path '%s'\n", fsopt->server_path);

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
	size_t pos;
	int ret;

	/* a comma between MNT/MS and client options */
	seq_putc(m, ',');
	pos = m->count;

	ret = ceph_print_client_options(m, fsc->client);
	if (ret)
		return ret;

	/* retract our comma if no client options */
	if (m->count == pos)
		m->count--;

	if (fsopt->flags & CEPH_MOUNT_OPT_DIRSTAT)
		seq_puts(m, ",dirstat");
	if ((fsopt->flags & CEPH_MOUNT_OPT_RBYTES))
		seq_puts(m, ",rbytes");
	if (fsopt->flags & CEPH_MOUNT_OPT_NOASYNCREADDIR)
		seq_puts(m, ",noasyncreaddir");
	if ((fsopt->flags & CEPH_MOUNT_OPT_DCACHE) == 0)
		seq_puts(m, ",nodcache");
	if (fsopt->flags & CEPH_MOUNT_OPT_FSCACHE) {
		seq_show_option(m, "fsc", fsopt->fscache_uniq);
	}
	if (fsopt->flags & CEPH_MOUNT_OPT_NOPOOLPERM)
		seq_puts(m, ",nopoolperm");
	if (fsopt->flags & CEPH_MOUNT_OPT_NOQUOTADF)
		seq_puts(m, ",noquotadf");

#ifdef CONFIG_CEPH_FS_POSIX_ACL
	if (fsopt->sb_flags & SB_POSIXACL)
		seq_puts(m, ",acl");
	else
		seq_puts(m, ",noacl");
#endif

	if (fsopt->mds_namespace)
		seq_show_option(m, "mds_namespace", fsopt->mds_namespace);
	if (fsopt->wsize)
		seq_printf(m, ",wsize=%d", fsopt->wsize);
	if (fsopt->rsize != CEPH_MAX_READ_SIZE)
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
	if (fsopt->max_readdir != CEPH_MAX_READDIR_DEFAULT)
		seq_printf(m, ",readdir_max_entries=%d", fsopt->max_readdir);
	if (fsopt->max_readdir_bytes != CEPH_MAX_READDIR_BYTES_DEFAULT)
		seq_printf(m, ",readdir_max_bytes=%d", fsopt->max_readdir_bytes);
	if (strcmp(fsopt->snapdir_name, CEPH_SNAPDIRNAME_DEFAULT))
		seq_show_option(m, "snapdirname", fsopt->snapdir_name);

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
		ceph_mdsc_handle_mdsmap(fsc->mdsc, msg);
		return 0;
	case CEPH_MSG_FS_MAP_USER:
		ceph_mdsc_handle_fsmap(fsc->mdsc, msg);
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
	int page_count;
	size_t size;
	int err = -ENOMEM;

	fsc = kzalloc(sizeof(*fsc), GFP_KERNEL);
	if (!fsc)
		return ERR_PTR(-ENOMEM);

	fsc->client = ceph_create_client(opt, fsc);
	if (IS_ERR(fsc->client)) {
		err = PTR_ERR(fsc->client);
		goto fail;
	}
	fsc->client->extra_mon_dispatch = extra_mon_dispatch;

	if (!fsopt->mds_namespace) {
		ceph_monc_want_map(&fsc->client->monc, CEPH_SUB_MDSMAP,
				   0, true);
	} else {
		ceph_monc_want_map(&fsc->client->monc, CEPH_SUB_FSMAP,
				   0, false);
	}

	fsc->mount_options = fsopt;

	fsc->sb = NULL;
	fsc->mount_state = CEPH_MOUNT_MOUNTING;

	atomic_long_set(&fsc->writeback_count, 0);

	err = -ENOMEM;
	/*
	 * The number of concurrent works can be high but they don't need
	 * to be processed in parallel, limit concurrency.
	 */
	fsc->wb_wq = alloc_workqueue("ceph-writeback", 0, 1);
	if (!fsc->wb_wq)
		goto fail_client;
	fsc->pg_inv_wq = alloc_workqueue("ceph-pg-invalid", 0, 1);
	if (!fsc->pg_inv_wq)
		goto fail_wb_wq;
	fsc->trunc_wq = alloc_workqueue("ceph-trunc", 0, 1);
	if (!fsc->trunc_wq)
		goto fail_pg_inv_wq;

	/* set up mempools */
	err = -ENOMEM;
	page_count = fsc->mount_options->wsize >> PAGE_SHIFT;
	size = sizeof (struct page *) * (page_count ? page_count : 1);
	fsc->wb_pagevec_pool = mempool_create_kmalloc_pool(10, size);
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

	mempool_destroy(fsc->wb_pagevec_pool);

	destroy_mount_options(fsc->mount_options);

	ceph_destroy_client(fsc->client);

	kfree(fsc);
	dout("destroy_fs_client %p done\n", fsc);
}

/*
 * caches
 */
struct kmem_cache *ceph_inode_cachep;
struct kmem_cache *ceph_cap_cachep;
struct kmem_cache *ceph_cap_flush_cachep;
struct kmem_cache *ceph_dentry_cachep;
struct kmem_cache *ceph_file_cachep;
struct kmem_cache *ceph_dir_file_cachep;

static void ceph_inode_init_once(void *foo)
{
	struct ceph_inode_info *ci = foo;
	inode_init_once(&ci->vfs_inode);
}

static int __init init_caches(void)
{
	int error = -ENOMEM;

	ceph_inode_cachep = kmem_cache_create("ceph_inode_info",
				      sizeof(struct ceph_inode_info),
				      __alignof__(struct ceph_inode_info),
				      SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD|
				      SLAB_ACCOUNT, ceph_inode_init_once);
	if (!ceph_inode_cachep)
		return -ENOMEM;

	ceph_cap_cachep = KMEM_CACHE(ceph_cap, SLAB_MEM_SPREAD);
	if (!ceph_cap_cachep)
		goto bad_cap;
	ceph_cap_flush_cachep = KMEM_CACHE(ceph_cap_flush,
					   SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD);
	if (!ceph_cap_flush_cachep)
		goto bad_cap_flush;

	ceph_dentry_cachep = KMEM_CACHE(ceph_dentry_info,
					SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD);
	if (!ceph_dentry_cachep)
		goto bad_dentry;

	ceph_file_cachep = KMEM_CACHE(ceph_file_info, SLAB_MEM_SPREAD);
	if (!ceph_file_cachep)
		goto bad_file;

	ceph_dir_file_cachep = KMEM_CACHE(ceph_dir_file_info, SLAB_MEM_SPREAD);
	if (!ceph_dir_file_cachep)
		goto bad_dir_file;

	error = ceph_fscache_register();
	if (error)
		goto bad_fscache;

	return 0;

bad_fscache:
	kmem_cache_destroy(ceph_dir_file_cachep);
bad_dir_file:
	kmem_cache_destroy(ceph_file_cachep);
bad_file:
	kmem_cache_destroy(ceph_dentry_cachep);
bad_dentry:
	kmem_cache_destroy(ceph_cap_flush_cachep);
bad_cap_flush:
	kmem_cache_destroy(ceph_cap_cachep);
bad_cap:
	kmem_cache_destroy(ceph_inode_cachep);
	return error;
}

static void destroy_caches(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();

	kmem_cache_destroy(ceph_inode_cachep);
	kmem_cache_destroy(ceph_cap_cachep);
	kmem_cache_destroy(ceph_cap_flush_cachep);
	kmem_cache_destroy(ceph_dentry_cachep);
	kmem_cache_destroy(ceph_file_cachep);
	kmem_cache_destroy(ceph_dir_file_cachep);

	ceph_fscache_unregister();
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
	ceph_mdsc_force_umount(fsc->mdsc);
	return;
}

static const struct super_operations ceph_super_ops = {
	.alloc_inode	= ceph_alloc_inode,
	.destroy_inode	= ceph_destroy_inode,
	.write_inode    = ceph_write_inode,
	.drop_inode	= ceph_drop_inode,
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
	if (!req->r_path1) {
		root = ERR_PTR(-ENOMEM);
		goto out;
	}

	req->r_ino1.ino = CEPH_INO_ROOT;
	req->r_ino1.snap = CEPH_NOSNAP;
	req->r_started = started;
	req->r_timeout = fsc->client->options->mount_timeout;
	req->r_args.getattr.mask = cpu_to_le32(CEPH_STAT_CAP_INODE);
	req->r_num_caps = 2;
	err = ceph_mdsc_do_request(mdsc, NULL, req);
	if (err == 0) {
		struct inode *inode = req->r_target_inode;
		req->r_target_inode = NULL;
		dout("open_root_inode success\n");
		root = d_make_root(inode);
		if (!root) {
			root = ERR_PTR(-ENOMEM);
			goto out;
		}
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
static struct dentry *ceph_real_mount(struct ceph_fs_client *fsc)
{
	int err;
	unsigned long started = jiffies;  /* note the start time */
	struct dentry *root;

	dout("mount start %p\n", fsc);
	mutex_lock(&fsc->client->mount_mutex);

	if (!fsc->sb->s_root) {
		const char *path;
		err = __ceph_open_session(fsc->client, started);
		if (err < 0)
			goto out;

		/* setup fscache */
		if (fsc->mount_options->flags & CEPH_MOUNT_OPT_FSCACHE) {
			err = ceph_fscache_register_fs(fsc);
			if (err < 0)
				goto out;
		}

		if (!fsc->mount_options->server_path) {
			path = "";
			dout("mount opening path \\t\n");
		} else {
			path = fsc->mount_options->server_path + 1;
			dout("mount opening path %s\n", path);
		}

		err = ceph_fs_debugfs_init(fsc);
		if (err < 0)
			goto out;

		root = open_root_dentry(fsc, path, started);
		if (IS_ERR(root)) {
			err = PTR_ERR(root);
			goto out;
		}
		fsc->sb->s_root = dget(root);
	} else {
		root = dget(fsc->sb->s_root);
	}

	fsc->mount_state = CEPH_MOUNT_MOUNTED;
	dout("mount success\n");
	mutex_unlock(&fsc->client->mount_mutex);
	return root;

out:
	mutex_unlock(&fsc->client->mount_mutex);
	return ERR_PTR(err);
}

static int ceph_set_super(struct super_block *s, void *data)
{
	struct ceph_fs_client *fsc = data;
	int ret;

	dout("set_super %p data %p\n", s, data);

	s->s_flags = fsc->mount_options->sb_flags;
	s->s_maxbytes = 1ULL << 40;  /* temp value until we get mdsmap */

	s->s_xattr = ceph_xattr_handlers;
	s->s_fs_info = fsc;
	fsc->sb = s;

	s->s_op = &ceph_super_ops;
	s->s_d_op = &ceph_dentry_ops;
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

static int ceph_setup_bdi(struct super_block *sb, struct ceph_fs_client *fsc)
{
	int err;

	err = super_setup_bdi_name(sb, "ceph-%ld",
				   atomic_long_inc_return(&bdi_seq));
	if (err)
		return err;

	/* set ra_pages based on rasize mount option? */
	sb->s_bdi->ra_pages = fsc->mount_options->rasize >> PAGE_SHIFT;

	/* set io_pages based on max osd read size */
	sb->s_bdi->io_pages = fsc->mount_options->rsize >> PAGE_SHIFT;

	return 0;
}

static struct dentry *ceph_mount(struct file_system_type *fs_type,
		       int flags, const char *dev_name, void *data)
{
	struct super_block *sb;
	struct ceph_fs_client *fsc;
	struct dentry *res;
	int err;
	int (*compare_super)(struct super_block *, void *) = ceph_compare_super;
	struct ceph_mount_options *fsopt = NULL;
	struct ceph_options *opt = NULL;

	dout("ceph_mount\n");

#ifdef CONFIG_CEPH_FS_POSIX_ACL
	flags |= SB_POSIXACL;
#endif
	err = parse_mount_options(&fsopt, &opt, flags, data, dev_name);
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
	sb = sget(fs_type, compare_super, ceph_set_super, flags, fsc);
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
		err = ceph_setup_bdi(sb, fsc);
		if (err < 0) {
			res = ERR_PTR(err);
			goto out_splat;
		}
	}

	res = ceph_real_mount(fsc);
	if (IS_ERR(res))
		goto out_splat;
	dout("root %p inode %p ino %llx.%llx\n", res,
	     d_inode(res), ceph_vinop(d_inode(res)));
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
	dev_t dev = s->s_dev;

	dout("kill_sb %p\n", s);

	ceph_mdsc_pre_umount(fsc->mdsc);
	generic_shutdown_super(s);

	fsc->client->extra_mon_dispatch = NULL;
	ceph_fs_debugfs_cleanup(fsc);

	ceph_fscache_unregister_fs(fsc);

	ceph_mdsc_destroy(fsc);

	destroy_fs_client(fsc);
	free_anon_bdev(dev);
}

static struct file_system_type ceph_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ceph",
	.mount		= ceph_mount,
	.kill_sb	= ceph_kill_sb,
	.fs_flags	= FS_RENAME_DOES_D_MOVE,
};
MODULE_ALIAS_FS("ceph");

static int __init init_ceph(void)
{
	int ret = init_caches();
	if (ret)
		goto out;

	ceph_flock_init();
	ceph_xattr_init();
	ret = register_filesystem(&ceph_fs_type);
	if (ret)
		goto out_xattr;

	pr_info("loaded (mds proto %d)\n", CEPH_MDSC_PROTOCOL);

	return 0;

out_xattr:
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
