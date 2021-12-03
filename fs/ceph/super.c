// SPDX-License-Identifier: GPL-2.0-only

#include <linux/ceph/ceph_debug.h>

#include <linux/backing-dev.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/inet.h>
#include <linux/in6.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
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

static DEFINE_SPINLOCK(ceph_fsc_lock);
static LIST_HEAD(ceph_fsc_list);

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
	struct ceph_mon_client *monc = &fsc->client->monc;
	struct ceph_statfs st;
	int i, err;
	u64 data_pool;

	if (fsc->mdsc->mdsmap->m_num_data_pg_pools == 1) {
		data_pool = fsc->mdsc->mdsmap->m_data_pg_pools[0];
	} else {
		data_pool = CEPH_NOPOOL;
	}

	dout("statfs\n");
	err = ceph_monc_do_statfs(monc, data_pool, &st);
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
	buf->f_fsid.val[0] = 0;
	mutex_lock(&monc->mutex);
	for (i = 0 ; i < sizeof(monc->monmap->fsid) / sizeof(__le32) ; ++i)
		buf->f_fsid.val[0] ^= le32_to_cpu(((__le32 *)&monc->monmap->fsid)[i]);
	mutex_unlock(&monc->mutex);

	/* fold the fs_cluster_id into the upper bits */
	buf->f_fsid.val[1] = monc->fs_cluster_id;

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
	Opt_caps_max,
	Opt_readdir_max_entries,
	Opt_readdir_max_bytes,
	Opt_congestion_kb,
	/* int args above */
	Opt_snapdirname,
	Opt_mds_namespace,
	Opt_recover_session,
	Opt_source,
	/* string args above */
	Opt_dirstat,
	Opt_rbytes,
	Opt_asyncreaddir,
	Opt_dcache,
	Opt_ino32,
	Opt_fscache,
	Opt_poolperm,
	Opt_require_active_mds,
	Opt_acl,
	Opt_quotadf,
	Opt_copyfrom,
	Opt_wsync,
};

enum ceph_recover_session_mode {
	ceph_recover_session_no,
	ceph_recover_session_clean
};

static const struct constant_table ceph_param_recover[] = {
	{ "no",		ceph_recover_session_no },
	{ "clean",	ceph_recover_session_clean },
	{}
};

static const struct fs_parameter_spec ceph_mount_parameters[] = {
	fsparam_flag_no ("acl",				Opt_acl),
	fsparam_flag_no ("asyncreaddir",		Opt_asyncreaddir),
	fsparam_s32	("caps_max",			Opt_caps_max),
	fsparam_u32	("caps_wanted_delay_max",	Opt_caps_wanted_delay_max),
	fsparam_u32	("caps_wanted_delay_min",	Opt_caps_wanted_delay_min),
	fsparam_u32	("write_congestion_kb",		Opt_congestion_kb),
	fsparam_flag_no ("copyfrom",			Opt_copyfrom),
	fsparam_flag_no ("dcache",			Opt_dcache),
	fsparam_flag_no ("dirstat",			Opt_dirstat),
	fsparam_flag_no	("fsc",				Opt_fscache), // fsc|nofsc
	fsparam_string	("fsc",				Opt_fscache), // fsc=...
	fsparam_flag_no ("ino32",			Opt_ino32),
	fsparam_string	("mds_namespace",		Opt_mds_namespace),
	fsparam_flag_no ("poolperm",			Opt_poolperm),
	fsparam_flag_no ("quotadf",			Opt_quotadf),
	fsparam_u32	("rasize",			Opt_rasize),
	fsparam_flag_no ("rbytes",			Opt_rbytes),
	fsparam_u32	("readdir_max_bytes",		Opt_readdir_max_bytes),
	fsparam_u32	("readdir_max_entries",		Opt_readdir_max_entries),
	fsparam_enum	("recover_session",		Opt_recover_session, ceph_param_recover),
	fsparam_flag_no ("require_active_mds",		Opt_require_active_mds),
	fsparam_u32	("rsize",			Opt_rsize),
	fsparam_string	("snapdirname",			Opt_snapdirname),
	fsparam_string	("source",			Opt_source),
	fsparam_u32	("wsize",			Opt_wsize),
	fsparam_flag_no	("wsync",			Opt_wsync),
	{}
};

struct ceph_parse_opts_ctx {
	struct ceph_options		*copts;
	struct ceph_mount_options	*opts;
};

/*
 * Remove adjacent slashes and then the trailing slash, unless it is
 * the only remaining character.
 *
 * E.g. "//dir1////dir2///" --> "/dir1/dir2", "///" --> "/".
 */
static void canonicalize_path(char *path)
{
	int i, j = 0;

	for (i = 0; path[i] != '\0'; i++) {
		if (path[i] != '/' || j < 1 || path[j - 1] != '/')
			path[j++] = path[i];
	}

	if (j > 1 && path[j - 1] == '/')
		j--;
	path[j] = '\0';
}

/*
 * Parse the source parameter.  Distinguish the server list from the path.
 *
 * The source will look like:
 *     <server_spec>[,<server_spec>...]:[<path>]
 * where
 *     <server_spec> is <ip>[:<port>]
 *     <path> is optional, but if present must begin with '/'
 */
static int ceph_parse_source(struct fs_parameter *param, struct fs_context *fc)
{
	struct ceph_parse_opts_ctx *pctx = fc->fs_private;
	struct ceph_mount_options *fsopt = pctx->opts;
	char *dev_name = param->string, *dev_name_end;
	int ret;

	dout("%s '%s'\n", __func__, dev_name);
	if (!dev_name || !*dev_name)
		return invalfc(fc, "Empty source");

	dev_name_end = strchr(dev_name, '/');
	if (dev_name_end) {
		/*
		 * The server_path will include the whole chars from userland
		 * including the leading '/'.
		 */
		kfree(fsopt->server_path);
		fsopt->server_path = kstrdup(dev_name_end, GFP_KERNEL);
		if (!fsopt->server_path)
			return -ENOMEM;

		canonicalize_path(fsopt->server_path);
	} else {
		dev_name_end = dev_name + strlen(dev_name);
	}

	dev_name_end--;		/* back up to ':' separator */
	if (dev_name_end < dev_name || *dev_name_end != ':')
		return invalfc(fc, "No path or : separator in source");

	dout("device name '%.*s'\n", (int)(dev_name_end - dev_name), dev_name);
	if (fsopt->server_path)
		dout("server path '%s'\n", fsopt->server_path);

	ret = ceph_parse_mon_ips(param->string, dev_name_end - dev_name,
				 pctx->copts, fc->log.log);
	if (ret)
		return ret;

	fc->source = param->string;
	param->string = NULL;
	return 0;
}

static int ceph_parse_mount_param(struct fs_context *fc,
				  struct fs_parameter *param)
{
	struct ceph_parse_opts_ctx *pctx = fc->fs_private;
	struct ceph_mount_options *fsopt = pctx->opts;
	struct fs_parse_result result;
	unsigned int mode;
	int token, ret;

	ret = ceph_parse_param(param, pctx->copts, fc->log.log);
	if (ret != -ENOPARAM)
		return ret;

	token = fs_parse(fc, ceph_mount_parameters, param, &result);
	dout("%s fs_parse '%s' token %d\n", __func__, param->key, token);
	if (token < 0)
		return token;

	switch (token) {
	case Opt_snapdirname:
		kfree(fsopt->snapdir_name);
		fsopt->snapdir_name = param->string;
		param->string = NULL;
		break;
	case Opt_mds_namespace:
		kfree(fsopt->mds_namespace);
		fsopt->mds_namespace = param->string;
		param->string = NULL;
		break;
	case Opt_recover_session:
		mode = result.uint_32;
		if (mode == ceph_recover_session_no)
			fsopt->flags &= ~CEPH_MOUNT_OPT_CLEANRECOVER;
		else if (mode == ceph_recover_session_clean)
			fsopt->flags |= CEPH_MOUNT_OPT_CLEANRECOVER;
		else
			BUG();
		break;
	case Opt_source:
		if (fc->source)
			return invalfc(fc, "Multiple sources specified");
		return ceph_parse_source(param, fc);
	case Opt_wsize:
		if (result.uint_32 < PAGE_SIZE ||
		    result.uint_32 > CEPH_MAX_WRITE_SIZE)
			goto out_of_range;
		fsopt->wsize = ALIGN(result.uint_32, PAGE_SIZE);
		break;
	case Opt_rsize:
		if (result.uint_32 < PAGE_SIZE ||
		    result.uint_32 > CEPH_MAX_READ_SIZE)
			goto out_of_range;
		fsopt->rsize = ALIGN(result.uint_32, PAGE_SIZE);
		break;
	case Opt_rasize:
		fsopt->rasize = ALIGN(result.uint_32, PAGE_SIZE);
		break;
	case Opt_caps_wanted_delay_min:
		if (result.uint_32 < 1)
			goto out_of_range;
		fsopt->caps_wanted_delay_min = result.uint_32;
		break;
	case Opt_caps_wanted_delay_max:
		if (result.uint_32 < 1)
			goto out_of_range;
		fsopt->caps_wanted_delay_max = result.uint_32;
		break;
	case Opt_caps_max:
		if (result.int_32 < 0)
			goto out_of_range;
		fsopt->caps_max = result.int_32;
		break;
	case Opt_readdir_max_entries:
		if (result.uint_32 < 1)
			goto out_of_range;
		fsopt->max_readdir = result.uint_32;
		break;
	case Opt_readdir_max_bytes:
		if (result.uint_32 < PAGE_SIZE && result.uint_32 != 0)
			goto out_of_range;
		fsopt->max_readdir_bytes = result.uint_32;
		break;
	case Opt_congestion_kb:
		if (result.uint_32 < 1024) /* at least 1M */
			goto out_of_range;
		fsopt->congestion_kb = result.uint_32;
		break;
	case Opt_dirstat:
		if (!result.negated)
			fsopt->flags |= CEPH_MOUNT_OPT_DIRSTAT;
		else
			fsopt->flags &= ~CEPH_MOUNT_OPT_DIRSTAT;
		break;
	case Opt_rbytes:
		if (!result.negated)
			fsopt->flags |= CEPH_MOUNT_OPT_RBYTES;
		else
			fsopt->flags &= ~CEPH_MOUNT_OPT_RBYTES;
		break;
	case Opt_asyncreaddir:
		if (!result.negated)
			fsopt->flags &= ~CEPH_MOUNT_OPT_NOASYNCREADDIR;
		else
			fsopt->flags |= CEPH_MOUNT_OPT_NOASYNCREADDIR;
		break;
	case Opt_dcache:
		if (!result.negated)
			fsopt->flags |= CEPH_MOUNT_OPT_DCACHE;
		else
			fsopt->flags &= ~CEPH_MOUNT_OPT_DCACHE;
		break;
	case Opt_ino32:
		if (!result.negated)
			fsopt->flags |= CEPH_MOUNT_OPT_INO32;
		else
			fsopt->flags &= ~CEPH_MOUNT_OPT_INO32;
		break;

	case Opt_fscache:
#ifdef CONFIG_CEPH_FSCACHE
		kfree(fsopt->fscache_uniq);
		fsopt->fscache_uniq = NULL;
		if (result.negated) {
			fsopt->flags &= ~CEPH_MOUNT_OPT_FSCACHE;
		} else {
			fsopt->flags |= CEPH_MOUNT_OPT_FSCACHE;
			fsopt->fscache_uniq = param->string;
			param->string = NULL;
		}
		break;
#else
		return invalfc(fc, "fscache support is disabled");
#endif
	case Opt_poolperm:
		if (!result.negated)
			fsopt->flags &= ~CEPH_MOUNT_OPT_NOPOOLPERM;
		else
			fsopt->flags |= CEPH_MOUNT_OPT_NOPOOLPERM;
		break;
	case Opt_require_active_mds:
		if (!result.negated)
			fsopt->flags &= ~CEPH_MOUNT_OPT_MOUNTWAIT;
		else
			fsopt->flags |= CEPH_MOUNT_OPT_MOUNTWAIT;
		break;
	case Opt_quotadf:
		if (!result.negated)
			fsopt->flags &= ~CEPH_MOUNT_OPT_NOQUOTADF;
		else
			fsopt->flags |= CEPH_MOUNT_OPT_NOQUOTADF;
		break;
	case Opt_copyfrom:
		if (!result.negated)
			fsopt->flags &= ~CEPH_MOUNT_OPT_NOCOPYFROM;
		else
			fsopt->flags |= CEPH_MOUNT_OPT_NOCOPYFROM;
		break;
	case Opt_acl:
		if (!result.negated) {
#ifdef CONFIG_CEPH_FS_POSIX_ACL
			fc->sb_flags |= SB_POSIXACL;
#else
			return invalfc(fc, "POSIX ACL support is disabled");
#endif
		} else {
			fc->sb_flags &= ~SB_POSIXACL;
		}
		break;
	case Opt_wsync:
		if (!result.negated)
			fsopt->flags &= ~CEPH_MOUNT_OPT_ASYNC_DIROPS;
		else
			fsopt->flags |= CEPH_MOUNT_OPT_ASYNC_DIROPS;
		break;
	default:
		BUG();
	}
	return 0;

out_of_range:
	return invalfc(fc, "%s out of range", param->key);
}

static void destroy_mount_options(struct ceph_mount_options *args)
{
	dout("destroy_mount_options %p\n", args);
	if (!args)
		return;

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

	ret = ceph_print_client_options(m, fsc->client, false);
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
	if (fsopt->flags & CEPH_MOUNT_OPT_INO32)
		seq_puts(m, ",ino32");
	if (fsopt->flags & CEPH_MOUNT_OPT_FSCACHE) {
		seq_show_option(m, "fsc", fsopt->fscache_uniq);
	}
	if (fsopt->flags & CEPH_MOUNT_OPT_NOPOOLPERM)
		seq_puts(m, ",nopoolperm");
	if (fsopt->flags & CEPH_MOUNT_OPT_NOQUOTADF)
		seq_puts(m, ",noquotadf");

#ifdef CONFIG_CEPH_FS_POSIX_ACL
	if (root->d_sb->s_flags & SB_POSIXACL)
		seq_puts(m, ",acl");
	else
		seq_puts(m, ",noacl");
#endif

	if ((fsopt->flags & CEPH_MOUNT_OPT_NOCOPYFROM) == 0)
		seq_puts(m, ",copyfrom");

	if (fsopt->mds_namespace)
		seq_show_option(m, "mds_namespace", fsopt->mds_namespace);

	if (fsopt->flags & CEPH_MOUNT_OPT_CLEANRECOVER)
		seq_show_option(m, "recover_session", "clean");

	if (fsopt->flags & CEPH_MOUNT_OPT_ASYNC_DIROPS)
		seq_puts(m, ",nowsync");

	if (fsopt->wsize != CEPH_MAX_WRITE_SIZE)
		seq_printf(m, ",wsize=%u", fsopt->wsize);
	if (fsopt->rsize != CEPH_MAX_READ_SIZE)
		seq_printf(m, ",rsize=%u", fsopt->rsize);
	if (fsopt->rasize != CEPH_RASIZE_DEFAULT)
		seq_printf(m, ",rasize=%u", fsopt->rasize);
	if (fsopt->congestion_kb != default_congestion_kb())
		seq_printf(m, ",write_congestion_kb=%u", fsopt->congestion_kb);
	if (fsopt->caps_max)
		seq_printf(m, ",caps_max=%d", fsopt->caps_max);
	if (fsopt->caps_wanted_delay_min != CEPH_CAPS_WANTED_DELAY_MIN_DEFAULT)
		seq_printf(m, ",caps_wanted_delay_min=%u",
			 fsopt->caps_wanted_delay_min);
	if (fsopt->caps_wanted_delay_max != CEPH_CAPS_WANTED_DELAY_MAX_DEFAULT)
		seq_printf(m, ",caps_wanted_delay_max=%u",
			   fsopt->caps_wanted_delay_max);
	if (fsopt->max_readdir != CEPH_MAX_READDIR_DEFAULT)
		seq_printf(m, ",readdir_max_entries=%u", fsopt->max_readdir);
	if (fsopt->max_readdir_bytes != CEPH_MAX_READDIR_BYTES_DEFAULT)
		seq_printf(m, ",readdir_max_bytes=%u", fsopt->max_readdir_bytes);
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
 *
 * Success or not, this function consumes @fsopt and @opt.
 */
static struct ceph_fs_client *create_fs_client(struct ceph_mount_options *fsopt,
					struct ceph_options *opt)
{
	struct ceph_fs_client *fsc;
	int err;

	fsc = kzalloc(sizeof(*fsc), GFP_KERNEL);
	if (!fsc) {
		err = -ENOMEM;
		goto fail;
	}

	fsc->client = ceph_create_client(opt, fsc);
	if (IS_ERR(fsc->client)) {
		err = PTR_ERR(fsc->client);
		goto fail;
	}
	opt = NULL; /* fsc->client now owns this */

	fsc->client->extra_mon_dispatch = extra_mon_dispatch;
	ceph_set_opt(fsc->client, ABORT_ON_FULL);

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
	fsc->filp_gen = 1;
	fsc->have_copy_from2 = true;

	atomic_long_set(&fsc->writeback_count, 0);

	err = -ENOMEM;
	/*
	 * The number of concurrent works can be high but they don't need
	 * to be processed in parallel, limit concurrency.
	 */
	fsc->inode_wq = alloc_workqueue("ceph-inode", WQ_UNBOUND, 0);
	if (!fsc->inode_wq)
		goto fail_client;
	fsc->cap_wq = alloc_workqueue("ceph-cap", 0, 1);
	if (!fsc->cap_wq)
		goto fail_inode_wq;

	spin_lock(&ceph_fsc_lock);
	list_add_tail(&fsc->metric_wakeup, &ceph_fsc_list);
	spin_unlock(&ceph_fsc_lock);

	return fsc;

fail_inode_wq:
	destroy_workqueue(fsc->inode_wq);
fail_client:
	ceph_destroy_client(fsc->client);
fail:
	kfree(fsc);
	if (opt)
		ceph_destroy_options(opt);
	destroy_mount_options(fsopt);
	return ERR_PTR(err);
}

static void flush_fs_workqueues(struct ceph_fs_client *fsc)
{
	flush_workqueue(fsc->inode_wq);
	flush_workqueue(fsc->cap_wq);
}

static void destroy_fs_client(struct ceph_fs_client *fsc)
{
	dout("destroy_fs_client %p\n", fsc);

	spin_lock(&ceph_fsc_lock);
	list_del(&fsc->metric_wakeup);
	spin_unlock(&ceph_fsc_lock);

	ceph_mdsc_destroy(fsc);
	destroy_workqueue(fsc->inode_wq);
	destroy_workqueue(fsc->cap_wq);

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
struct kmem_cache *ceph_mds_request_cachep;
mempool_t *ceph_wb_pagevec_pool;

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

	ceph_mds_request_cachep = KMEM_CACHE(ceph_mds_request, SLAB_MEM_SPREAD);
	if (!ceph_mds_request_cachep)
		goto bad_mds_req;

	ceph_wb_pagevec_pool = mempool_create_kmalloc_pool(10, CEPH_MAX_WRITE_SIZE >> PAGE_SHIFT);
	if (!ceph_wb_pagevec_pool)
		goto bad_pagevec_pool;

	error = ceph_fscache_register();
	if (error)
		goto bad_fscache;

	return 0;

bad_fscache:
	kmem_cache_destroy(ceph_mds_request_cachep);
bad_pagevec_pool:
	mempool_destroy(ceph_wb_pagevec_pool);
bad_mds_req:
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
	kmem_cache_destroy(ceph_mds_request_cachep);
	mempool_destroy(ceph_wb_pagevec_pool);

	ceph_fscache_unregister();
}

/*
 * ceph_umount_begin - initiate forced umount.  Tear down the
 * mount, skipping steps that may hang while waiting for server(s).
 */
static void ceph_umount_begin(struct super_block *sb)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(sb);

	dout("ceph_umount_begin - starting forced umount\n");
	if (!fsc)
		return;
	fsc->mount_state = CEPH_MOUNT_SHUTDOWN;
	ceph_osdc_abort_requests(&fsc->client->osdc, -EIO);
	ceph_mdsc_force_umount(fsc->mdsc);
	fsc->filp_gen++; // invalidate open files
}

static const struct super_operations ceph_super_ops = {
	.alloc_inode	= ceph_alloc_inode,
	.free_inode	= ceph_free_inode,
	.write_inode    = ceph_write_inode,
	.drop_inode	= generic_delete_inode,
	.evict_inode	= ceph_evict_inode,
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
static struct dentry *ceph_real_mount(struct ceph_fs_client *fsc,
				      struct fs_context *fc)
{
	int err;
	unsigned long started = jiffies;  /* note the start time */
	struct dentry *root;

	dout("mount start %p\n", fsc);
	mutex_lock(&fsc->client->mount_mutex);

	if (!fsc->sb->s_root) {
		const char *path = fsc->mount_options->server_path ?
				     fsc->mount_options->server_path + 1 : "";

		err = __ceph_open_session(fsc->client, started);
		if (err < 0)
			goto out;

		/* setup fscache */
		if (fsc->mount_options->flags & CEPH_MOUNT_OPT_FSCACHE) {
			err = ceph_fscache_register_fs(fsc, fc);
			if (err < 0)
				goto out;
		}

		dout("mount opening path '%s'\n", path);

		ceph_fs_debugfs_init(fsc);

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

static int ceph_set_super(struct super_block *s, struct fs_context *fc)
{
	struct ceph_fs_client *fsc = s->s_fs_info;
	int ret;

	dout("set_super %p\n", s);

	s->s_maxbytes = MAX_LFS_FILESIZE;

	s->s_xattr = ceph_xattr_handlers;
	fsc->sb = s;
	fsc->max_file_size = 1ULL << 40; /* temp value until we get mdsmap */

	s->s_op = &ceph_super_ops;
	s->s_d_op = &ceph_dentry_ops;
	s->s_export_op = &ceph_export_ops;

	s->s_time_gran = 1;
	s->s_time_min = 0;
	s->s_time_max = U32_MAX;

	ret = set_anon_super_fc(s, fc);
	if (ret != 0)
		fsc->sb = NULL;
	return ret;
}

/*
 * share superblock if same fs AND options
 */
static int ceph_compare_super(struct super_block *sb, struct fs_context *fc)
{
	struct ceph_fs_client *new = fc->s_fs_info;
	struct ceph_mount_options *fsopt = new->mount_options;
	struct ceph_options *opt = new->client->options;
	struct ceph_fs_client *fsc = ceph_sb_to_client(sb);

	dout("ceph_compare_super %p\n", sb);

	if (compare_mount_options(fsopt, opt, fsc)) {
		dout("monitor(s)/mount options don't match\n");
		return 0;
	}
	if ((opt->flags & CEPH_OPT_FSID) &&
	    ceph_fsid_compare(&opt->fsid, &fsc->client->fsid)) {
		dout("fsid doesn't match\n");
		return 0;
	}
	if (fc->sb_flags != (sb->s_flags & ~SB_BORN)) {
		dout("flags differ\n");
		return 0;
	}

	if (fsc->blocklisted && !ceph_test_mount_opt(fsc, CLEANRECOVER)) {
		dout("client is blocklisted (and CLEANRECOVER is not set)\n");
		return 0;
	}

	if (fsc->mount_state == CEPH_MOUNT_SHUTDOWN) {
		dout("client has been forcibly unmounted\n");
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

static int ceph_get_tree(struct fs_context *fc)
{
	struct ceph_parse_opts_ctx *pctx = fc->fs_private;
	struct super_block *sb;
	struct ceph_fs_client *fsc;
	struct dentry *res;
	int (*compare_super)(struct super_block *, struct fs_context *) =
		ceph_compare_super;
	int err;

	dout("ceph_get_tree\n");

	if (!fc->source)
		return invalfc(fc, "No source");

	/* create client (which we may/may not use) */
	fsc = create_fs_client(pctx->opts, pctx->copts);
	pctx->opts = NULL;
	pctx->copts = NULL;
	if (IS_ERR(fsc)) {
		err = PTR_ERR(fsc);
		goto out_final;
	}

	err = ceph_mdsc_init(fsc);
	if (err < 0)
		goto out;

	if (ceph_test_opt(fsc->client, NOSHARE))
		compare_super = NULL;

	fc->s_fs_info = fsc;
	sb = sget_fc(fc, compare_super, ceph_set_super);
	fc->s_fs_info = NULL;
	if (IS_ERR(sb)) {
		err = PTR_ERR(sb);
		goto out;
	}

	if (ceph_sb_to_client(sb) != fsc) {
		destroy_fs_client(fsc);
		fsc = ceph_sb_to_client(sb);
		dout("get_sb got existing client %p\n", fsc);
	} else {
		dout("get_sb using new client %p\n", fsc);
		err = ceph_setup_bdi(sb, fsc);
		if (err < 0)
			goto out_splat;
	}

	res = ceph_real_mount(fsc, fc);
	if (IS_ERR(res)) {
		err = PTR_ERR(res);
		goto out_splat;
	}
	dout("root %p inode %p ino %llx.%llx\n", res,
	     d_inode(res), ceph_vinop(d_inode(res)));
	fc->root = fsc->sb->s_root;
	return 0;

out_splat:
	if (!ceph_mdsmap_is_cluster_available(fsc->mdsc->mdsmap)) {
		pr_info("No mds server is up or the cluster is laggy\n");
		err = -EHOSTUNREACH;
	}

	ceph_mdsc_close_sessions(fsc->mdsc);
	deactivate_locked_super(sb);
	goto out_final;

out:
	destroy_fs_client(fsc);
out_final:
	dout("ceph_get_tree fail %d\n", err);
	return err;
}

static void ceph_free_fc(struct fs_context *fc)
{
	struct ceph_parse_opts_ctx *pctx = fc->fs_private;

	if (pctx) {
		destroy_mount_options(pctx->opts);
		ceph_destroy_options(pctx->copts);
		kfree(pctx);
	}
}

static int ceph_reconfigure_fc(struct fs_context *fc)
{
	struct ceph_parse_opts_ctx *pctx = fc->fs_private;
	struct ceph_mount_options *fsopt = pctx->opts;
	struct ceph_fs_client *fsc = ceph_sb_to_client(fc->root->d_sb);

	if (fsopt->flags & CEPH_MOUNT_OPT_ASYNC_DIROPS)
		ceph_set_mount_opt(fsc, ASYNC_DIROPS);
	else
		ceph_clear_mount_opt(fsc, ASYNC_DIROPS);

	sync_filesystem(fc->root->d_sb);
	return 0;
}

static const struct fs_context_operations ceph_context_ops = {
	.free		= ceph_free_fc,
	.parse_param	= ceph_parse_mount_param,
	.get_tree	= ceph_get_tree,
	.reconfigure	= ceph_reconfigure_fc,
};

/*
 * Set up the filesystem mount context.
 */
static int ceph_init_fs_context(struct fs_context *fc)
{
	struct ceph_parse_opts_ctx *pctx;
	struct ceph_mount_options *fsopt;

	pctx = kzalloc(sizeof(*pctx), GFP_KERNEL);
	if (!pctx)
		return -ENOMEM;

	pctx->copts = ceph_alloc_options();
	if (!pctx->copts)
		goto nomem;

	pctx->opts = kzalloc(sizeof(*pctx->opts), GFP_KERNEL);
	if (!pctx->opts)
		goto nomem;

	fsopt = pctx->opts;
	fsopt->flags = CEPH_MOUNT_OPT_DEFAULT;

	fsopt->wsize = CEPH_MAX_WRITE_SIZE;
	fsopt->rsize = CEPH_MAX_READ_SIZE;
	fsopt->rasize = CEPH_RASIZE_DEFAULT;
	fsopt->snapdir_name = kstrdup(CEPH_SNAPDIRNAME_DEFAULT, GFP_KERNEL);
	if (!fsopt->snapdir_name)
		goto nomem;

	fsopt->caps_wanted_delay_min = CEPH_CAPS_WANTED_DELAY_MIN_DEFAULT;
	fsopt->caps_wanted_delay_max = CEPH_CAPS_WANTED_DELAY_MAX_DEFAULT;
	fsopt->max_readdir = CEPH_MAX_READDIR_DEFAULT;
	fsopt->max_readdir_bytes = CEPH_MAX_READDIR_BYTES_DEFAULT;
	fsopt->congestion_kb = default_congestion_kb();

#ifdef CONFIG_CEPH_FS_POSIX_ACL
	fc->sb_flags |= SB_POSIXACL;
#endif

	fc->fs_private = pctx;
	fc->ops = &ceph_context_ops;
	return 0;

nomem:
	destroy_mount_options(pctx->opts);
	ceph_destroy_options(pctx->copts);
	kfree(pctx);
	return -ENOMEM;
}

static void ceph_kill_sb(struct super_block *s)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(s);

	dout("kill_sb %p\n", s);

	ceph_mdsc_pre_umount(fsc->mdsc);
	flush_fs_workqueues(fsc);

	kill_anon_super(s);

	fsc->client->extra_mon_dispatch = NULL;
	ceph_fs_debugfs_cleanup(fsc);

	ceph_fscache_unregister_fs(fsc);

	destroy_fs_client(fsc);
}

static struct file_system_type ceph_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ceph",
	.init_fs_context = ceph_init_fs_context,
	.kill_sb	= ceph_kill_sb,
	.fs_flags	= FS_RENAME_DOES_D_MOVE,
};
MODULE_ALIAS_FS("ceph");

int ceph_force_reconnect(struct super_block *sb)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(sb);
	int err = 0;

	ceph_umount_begin(sb);

	/* Make sure all page caches get invalidated.
	 * see remove_session_caps_cb() */
	flush_workqueue(fsc->inode_wq);

	/* In case that we were blocklisted. This also reset
	 * all mon/osd connections */
	ceph_reset_client_addr(fsc->client);

	ceph_osdc_clear_abort_err(&fsc->client->osdc);

	fsc->blocklisted = false;
	fsc->mount_state = CEPH_MOUNT_MOUNTED;

	if (sb->s_root) {
		err = __ceph_do_getattr(d_inode(sb->s_root), NULL,
					CEPH_STAT_CAP_INODE, true);
	}
	return err;
}

static int __init init_ceph(void)
{
	int ret = init_caches();
	if (ret)
		goto out;

	ceph_flock_init();
	ret = register_filesystem(&ceph_fs_type);
	if (ret)
		goto out_caches;

	pr_info("loaded (mds proto %d)\n", CEPH_MDSC_PROTOCOL);

	return 0;

out_caches:
	destroy_caches();
out:
	return ret;
}

static void __exit exit_ceph(void)
{
	dout("exit_ceph\n");
	unregister_filesystem(&ceph_fs_type);
	destroy_caches();
}

static int param_set_metrics(const char *val, const struct kernel_param *kp)
{
	struct ceph_fs_client *fsc;
	int ret;

	ret = param_set_bool(val, kp);
	if (ret) {
		pr_err("Failed to parse sending metrics switch value '%s'\n",
		       val);
		return ret;
	} else if (!disable_send_metrics) {
		// wake up all the mds clients
		spin_lock(&ceph_fsc_lock);
		list_for_each_entry(fsc, &ceph_fsc_list, metric_wakeup) {
			metric_schedule_delayed(&fsc->mdsc->metric);
		}
		spin_unlock(&ceph_fsc_lock);
	}

	return 0;
}

static const struct kernel_param_ops param_ops_metrics = {
	.set = param_set_metrics,
	.get = param_get_bool,
};

bool disable_send_metrics = false;
module_param_cb(disable_send_metrics, &param_ops_metrics, &disable_send_metrics, 0644);
MODULE_PARM_DESC(disable_send_metrics, "Enable sending perf metrics to ceph cluster (default: on)");

module_init(init_ceph);
module_exit(exit_ceph);

MODULE_AUTHOR("Sage Weil <sage@newdream.net>");
MODULE_AUTHOR("Yehuda Sadeh <yehuda@hq.newdream.net>");
MODULE_AUTHOR("Patience Warnick <patience@newdream.net>");
MODULE_DESCRIPTION("Ceph filesystem for Linux");
MODULE_LICENSE("GPL");
