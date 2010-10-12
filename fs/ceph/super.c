
#include "ceph_debug.h"

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

#include "decode.h"
#include "super.h"
#include "mon_client.h"
#include "auth.h"

/*
 * Ceph superblock operations
 *
 * Handle the basics of mounting, unmounting.
 */


/*
 * find filename portion of a path (/foo/bar/baz -> baz)
 */
const char *ceph_file_part(const char *s, int len)
{
	const char *e = s + len;

	while (e != s && *(e-1) != '/')
		e--;
	return e;
}


/*
 * super ops
 */
static void ceph_put_super(struct super_block *s)
{
	struct ceph_client *client = ceph_sb_to_client(s);

	dout("put_super\n");
	ceph_mdsc_close_sessions(&client->mdsc);

	/*
	 * ensure we release the bdi before put_anon_super releases
	 * the device name.
	 */
	if (s->s_bdi == &client->backing_dev_info) {
		bdi_unregister(&client->backing_dev_info);
		s->s_bdi = NULL;
	}

	return;
}

static int ceph_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct ceph_client *client = ceph_inode_to_client(dentry->d_inode);
	struct ceph_monmap *monmap = client->monc.monmap;
	struct ceph_statfs st;
	u64 fsid;
	int err;

	dout("statfs\n");
	err = ceph_monc_do_statfs(&client->monc, &st);
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
	buf->f_bfree = (le64_to_cpu(st.kb) - le64_to_cpu(st.kb_used)) >>
		(CEPH_BLOCK_SHIFT-10);
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
	struct ceph_client *client = ceph_sb_to_client(sb);

	if (!wait) {
		dout("sync_fs (non-blocking)\n");
		ceph_flush_dirty_caps(&client->mdsc);
		dout("sync_fs (non-blocking) done\n");
		return 0;
	}

	dout("sync_fs (blocking)\n");
	ceph_osdc_sync(&ceph_sb_to_client(sb)->osdc);
	ceph_mdsc_sync(&ceph_sb_to_client(sb)->mdsc);
	dout("sync_fs (blocking) done\n");
	return 0;
}

static int default_congestion_kb(void)
{
	int congestion_kb;

	/*
	 * Copied from NFS
	 *
	 * congestion size, scale with available memory.
	 *
	 *  64MB:    8192k
	 * 128MB:   11585k
	 * 256MB:   16384k
	 * 512MB:   23170k
	 *   1GB:   32768k
	 *   2GB:   46340k
	 *   4GB:   65536k
	 *   8GB:   92681k
	 *  16GB:  131072k
	 *
	 * This allows larger machines to have larger/more transfers.
	 * Limit the default to 256M
	 */
	congestion_kb = (16*int_sqrt(totalram_pages)) << (PAGE_SHIFT-10);
	if (congestion_kb > 256*1024)
		congestion_kb = 256*1024;

	return congestion_kb;
}

/**
 * ceph_show_options - Show mount options in /proc/mounts
 * @m: seq_file to write to
 * @mnt: mount descriptor
 */
static int ceph_show_options(struct seq_file *m, struct vfsmount *mnt)
{
	struct ceph_client *client = ceph_sb_to_client(mnt->mnt_sb);
	struct ceph_mount_args *args = client->mount_args;

	if (args->flags & CEPH_OPT_FSID)
		seq_printf(m, ",fsid=%pU", &args->fsid);
	if (args->flags & CEPH_OPT_NOSHARE)
		seq_puts(m, ",noshare");
	if (args->flags & CEPH_OPT_DIRSTAT)
		seq_puts(m, ",dirstat");
	if ((args->flags & CEPH_OPT_RBYTES) == 0)
		seq_puts(m, ",norbytes");
	if (args->flags & CEPH_OPT_NOCRC)
		seq_puts(m, ",nocrc");
	if (args->flags & CEPH_OPT_NOASYNCREADDIR)
		seq_puts(m, ",noasyncreaddir");

	if (args->mount_timeout != CEPH_MOUNT_TIMEOUT_DEFAULT)
		seq_printf(m, ",mount_timeout=%d", args->mount_timeout);
	if (args->osd_idle_ttl != CEPH_OSD_IDLE_TTL_DEFAULT)
		seq_printf(m, ",osd_idle_ttl=%d", args->osd_idle_ttl);
	if (args->osd_timeout != CEPH_OSD_TIMEOUT_DEFAULT)
		seq_printf(m, ",osdtimeout=%d", args->osd_timeout);
	if (args->osd_keepalive_timeout != CEPH_OSD_KEEPALIVE_DEFAULT)
		seq_printf(m, ",osdkeepalivetimeout=%d",
			 args->osd_keepalive_timeout);
	if (args->wsize)
		seq_printf(m, ",wsize=%d", args->wsize);
	if (args->rsize != CEPH_MOUNT_RSIZE_DEFAULT)
		seq_printf(m, ",rsize=%d", args->rsize);
	if (args->congestion_kb != default_congestion_kb())
		seq_printf(m, ",write_congestion_kb=%d", args->congestion_kb);
	if (args->caps_wanted_delay_min != CEPH_CAPS_WANTED_DELAY_MIN_DEFAULT)
		seq_printf(m, ",caps_wanted_delay_min=%d",
			 args->caps_wanted_delay_min);
	if (args->caps_wanted_delay_max != CEPH_CAPS_WANTED_DELAY_MAX_DEFAULT)
		seq_printf(m, ",caps_wanted_delay_max=%d",
			   args->caps_wanted_delay_max);
	if (args->cap_release_safety != CEPH_CAP_RELEASE_SAFETY_DEFAULT)
		seq_printf(m, ",cap_release_safety=%d",
			   args->cap_release_safety);
	if (args->max_readdir != CEPH_MAX_READDIR_DEFAULT)
		seq_printf(m, ",readdir_max_entries=%d", args->max_readdir);
	if (args->max_readdir_bytes != CEPH_MAX_READDIR_BYTES_DEFAULT)
		seq_printf(m, ",readdir_max_bytes=%d", args->max_readdir_bytes);
	if (strcmp(args->snapdir_name, CEPH_SNAPDIRNAME_DEFAULT))
		seq_printf(m, ",snapdirname=%s", args->snapdir_name);
	if (args->name)
		seq_printf(m, ",name=%s", args->name);
	if (args->secret)
		seq_puts(m, ",secret=<hidden>");
	return 0;
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
	struct ceph_client *client = ceph_sb_to_client(sb);

	dout("ceph_umount_begin - starting forced umount\n");
	if (!client)
		return;
	client->mount_state = CEPH_MOUNT_SHUTDOWN;
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


const char *ceph_msg_type_name(int type)
{
	switch (type) {
	case CEPH_MSG_SHUTDOWN: return "shutdown";
	case CEPH_MSG_PING: return "ping";
	case CEPH_MSG_AUTH: return "auth";
	case CEPH_MSG_AUTH_REPLY: return "auth_reply";
	case CEPH_MSG_MON_MAP: return "mon_map";
	case CEPH_MSG_MON_GET_MAP: return "mon_get_map";
	case CEPH_MSG_MON_SUBSCRIBE: return "mon_subscribe";
	case CEPH_MSG_MON_SUBSCRIBE_ACK: return "mon_subscribe_ack";
	case CEPH_MSG_STATFS: return "statfs";
	case CEPH_MSG_STATFS_REPLY: return "statfs_reply";
	case CEPH_MSG_MDS_MAP: return "mds_map";
	case CEPH_MSG_CLIENT_SESSION: return "client_session";
	case CEPH_MSG_CLIENT_RECONNECT: return "client_reconnect";
	case CEPH_MSG_CLIENT_REQUEST: return "client_request";
	case CEPH_MSG_CLIENT_REQUEST_FORWARD: return "client_request_forward";
	case CEPH_MSG_CLIENT_REPLY: return "client_reply";
	case CEPH_MSG_CLIENT_CAPS: return "client_caps";
	case CEPH_MSG_CLIENT_CAPRELEASE: return "client_cap_release";
	case CEPH_MSG_CLIENT_SNAP: return "client_snap";
	case CEPH_MSG_CLIENT_LEASE: return "client_lease";
	case CEPH_MSG_OSD_MAP: return "osd_map";
	case CEPH_MSG_OSD_OP: return "osd_op";
	case CEPH_MSG_OSD_OPREPLY: return "osd_opreply";
	default: return "unknown";
	}
}


/*
 * mount options
 */
enum {
	Opt_wsize,
	Opt_rsize,
	Opt_osdtimeout,
	Opt_osdkeepalivetimeout,
	Opt_mount_timeout,
	Opt_osd_idle_ttl,
	Opt_caps_wanted_delay_min,
	Opt_caps_wanted_delay_max,
	Opt_cap_release_safety,
	Opt_readdir_max_entries,
	Opt_readdir_max_bytes,
	Opt_congestion_kb,
	Opt_last_int,
	/* int args above */
	Opt_fsid,
	Opt_snapdirname,
	Opt_name,
	Opt_secret,
	Opt_last_string,
	/* string args above */
	Opt_ip,
	Opt_noshare,
	Opt_dirstat,
	Opt_nodirstat,
	Opt_rbytes,
	Opt_norbytes,
	Opt_nocrc,
	Opt_noasyncreaddir,
};

static match_table_t arg_tokens = {
	{Opt_wsize, "wsize=%d"},
	{Opt_rsize, "rsize=%d"},
	{Opt_osdtimeout, "osdtimeout=%d"},
	{Opt_osdkeepalivetimeout, "osdkeepalive=%d"},
	{Opt_mount_timeout, "mount_timeout=%d"},
	{Opt_osd_idle_ttl, "osd_idle_ttl=%d"},
	{Opt_caps_wanted_delay_min, "caps_wanted_delay_min=%d"},
	{Opt_caps_wanted_delay_max, "caps_wanted_delay_max=%d"},
	{Opt_cap_release_safety, "cap_release_safety=%d"},
	{Opt_readdir_max_entries, "readdir_max_entries=%d"},
	{Opt_readdir_max_bytes, "readdir_max_bytes=%d"},
	{Opt_congestion_kb, "write_congestion_kb=%d"},
	/* int args above */
	{Opt_fsid, "fsid=%s"},
	{Opt_snapdirname, "snapdirname=%s"},
	{Opt_name, "name=%s"},
	{Opt_secret, "secret=%s"},
	/* string args above */
	{Opt_ip, "ip=%s"},
	{Opt_noshare, "noshare"},
	{Opt_dirstat, "dirstat"},
	{Opt_nodirstat, "nodirstat"},
	{Opt_rbytes, "rbytes"},
	{Opt_norbytes, "norbytes"},
	{Opt_nocrc, "nocrc"},
	{Opt_noasyncreaddir, "noasyncreaddir"},
	{-1, NULL}
};

static int parse_fsid(const char *str, struct ceph_fsid *fsid)
{
	int i = 0;
	char tmp[3];
	int err = -EINVAL;
	int d;

	dout("parse_fsid '%s'\n", str);
	tmp[2] = 0;
	while (*str && i < 16) {
		if (ispunct(*str)) {
			str++;
			continue;
		}
		if (!isxdigit(str[0]) || !isxdigit(str[1]))
			break;
		tmp[0] = str[0];
		tmp[1] = str[1];
		if (sscanf(tmp, "%x", &d) < 1)
			break;
		fsid->fsid[i] = d & 0xff;
		i++;
		str += 2;
	}

	if (i == 16)
		err = 0;
	dout("parse_fsid ret %d got fsid %pU", err, fsid);
	return err;
}

static struct ceph_mount_args *parse_mount_args(int flags, char *options,
						const char *dev_name,
						const char **path)
{
	struct ceph_mount_args *args;
	const char *c;
	int err = -ENOMEM;
	substring_t argstr[MAX_OPT_ARGS];

	args = kzalloc(sizeof(*args), GFP_KERNEL);
	if (!args)
		return ERR_PTR(-ENOMEM);
	args->mon_addr = kcalloc(CEPH_MAX_MON, sizeof(*args->mon_addr),
				 GFP_KERNEL);
	if (!args->mon_addr)
		goto out;

	dout("parse_mount_args %p, dev_name '%s'\n", args, dev_name);

	/* start with defaults */
	args->sb_flags = flags;
	args->flags = CEPH_OPT_DEFAULT;
	args->osd_timeout = CEPH_OSD_TIMEOUT_DEFAULT;
	args->osd_keepalive_timeout = CEPH_OSD_KEEPALIVE_DEFAULT;
	args->mount_timeout = CEPH_MOUNT_TIMEOUT_DEFAULT; /* seconds */
	args->osd_idle_ttl = CEPH_OSD_IDLE_TTL_DEFAULT;   /* seconds */
	args->caps_wanted_delay_min = CEPH_CAPS_WANTED_DELAY_MIN_DEFAULT;
	args->caps_wanted_delay_max = CEPH_CAPS_WANTED_DELAY_MAX_DEFAULT;
	args->rsize = CEPH_MOUNT_RSIZE_DEFAULT;
	args->snapdir_name = kstrdup(CEPH_SNAPDIRNAME_DEFAULT, GFP_KERNEL);
	args->cap_release_safety = CEPH_CAP_RELEASE_SAFETY_DEFAULT;
	args->max_readdir = CEPH_MAX_READDIR_DEFAULT;
	args->max_readdir_bytes = CEPH_MAX_READDIR_BYTES_DEFAULT;
	args->congestion_kb = default_congestion_kb();

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

	/* get mon ip(s) */
	err = ceph_parse_ips(dev_name, *path, args->mon_addr,
			     CEPH_MAX_MON, &args->num_mon);
	if (err < 0)
		goto out;

	/* path on server */
	*path += 2;
	dout("server path '%s'\n", *path);

	/* parse mount options */
	while ((c = strsep(&options, ",")) != NULL) {
		int token, intval, ret;
		if (!*c)
			continue;
		err = -EINVAL;
		token = match_token((char *)c, arg_tokens, argstr);
		if (token < 0) {
			pr_err("bad mount option at '%s'\n", c);
			goto out;
		}
		if (token < Opt_last_int) {
			ret = match_int(&argstr[0], &intval);
			if (ret < 0) {
				pr_err("bad mount option arg (not int) "
				       "at '%s'\n", c);
				continue;
			}
			dout("got int token %d val %d\n", token, intval);
		} else if (token > Opt_last_int && token < Opt_last_string) {
			dout("got string token %d val %s\n", token,
			     argstr[0].from);
		} else {
			dout("got token %d\n", token);
		}
		switch (token) {
		case Opt_ip:
			err = ceph_parse_ips(argstr[0].from,
					     argstr[0].to,
					     &args->my_addr,
					     1, NULL);
			if (err < 0)
				goto out;
			args->flags |= CEPH_OPT_MYIP;
			break;

		case Opt_fsid:
			err = parse_fsid(argstr[0].from, &args->fsid);
			if (err == 0)
				args->flags |= CEPH_OPT_FSID;
			break;
		case Opt_snapdirname:
			kfree(args->snapdir_name);
			args->snapdir_name = kstrndup(argstr[0].from,
					      argstr[0].to-argstr[0].from,
					      GFP_KERNEL);
			break;
		case Opt_name:
			args->name = kstrndup(argstr[0].from,
					      argstr[0].to-argstr[0].from,
					      GFP_KERNEL);
			break;
		case Opt_secret:
			args->secret = kstrndup(argstr[0].from,
						argstr[0].to-argstr[0].from,
						GFP_KERNEL);
			break;

			/* misc */
		case Opt_wsize:
			args->wsize = intval;
			break;
		case Opt_rsize:
			args->rsize = intval;
			break;
		case Opt_osdtimeout:
			args->osd_timeout = intval;
			break;
		case Opt_osdkeepalivetimeout:
			args->osd_keepalive_timeout = intval;
			break;
		case Opt_osd_idle_ttl:
			args->osd_idle_ttl = intval;
			break;
		case Opt_mount_timeout:
			args->mount_timeout = intval;
			break;
		case Opt_caps_wanted_delay_min:
			args->caps_wanted_delay_min = intval;
			break;
		case Opt_caps_wanted_delay_max:
			args->caps_wanted_delay_max = intval;
			break;
		case Opt_readdir_max_entries:
			args->max_readdir = intval;
			break;
		case Opt_readdir_max_bytes:
			args->max_readdir_bytes = intval;
			break;
		case Opt_congestion_kb:
			args->congestion_kb = intval;
			break;

		case Opt_noshare:
			args->flags |= CEPH_OPT_NOSHARE;
			break;

		case Opt_dirstat:
			args->flags |= CEPH_OPT_DIRSTAT;
			break;
		case Opt_nodirstat:
			args->flags &= ~CEPH_OPT_DIRSTAT;
			break;
		case Opt_rbytes:
			args->flags |= CEPH_OPT_RBYTES;
			break;
		case Opt_norbytes:
			args->flags &= ~CEPH_OPT_RBYTES;
			break;
		case Opt_nocrc:
			args->flags |= CEPH_OPT_NOCRC;
			break;
		case Opt_noasyncreaddir:
			args->flags |= CEPH_OPT_NOASYNCREADDIR;
			break;

		default:
			BUG_ON(token);
		}
	}
	return args;

out:
	kfree(args->mon_addr);
	kfree(args);
	return ERR_PTR(err);
}

static void destroy_mount_args(struct ceph_mount_args *args)
{
	dout("destroy_mount_args %p\n", args);
	kfree(args->snapdir_name);
	args->snapdir_name = NULL;
	kfree(args->name);
	args->name = NULL;
	kfree(args->secret);
	args->secret = NULL;
	kfree(args);
}

/*
 * create a fresh client instance
 */
static struct ceph_client *ceph_create_client(struct ceph_mount_args *args)
{
	struct ceph_client *client;
	int err = -ENOMEM;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (client == NULL)
		return ERR_PTR(-ENOMEM);

	mutex_init(&client->mount_mutex);

	init_waitqueue_head(&client->auth_wq);

	client->sb = NULL;
	client->mount_state = CEPH_MOUNT_MOUNTING;
	client->mount_args = args;

	client->msgr = NULL;

	client->auth_err = 0;
	atomic_long_set(&client->writeback_count, 0);

	err = bdi_init(&client->backing_dev_info);
	if (err < 0)
		goto fail;

	err = -ENOMEM;
	client->wb_wq = create_workqueue("ceph-writeback");
	if (client->wb_wq == NULL)
		goto fail_bdi;
	client->pg_inv_wq = create_singlethread_workqueue("ceph-pg-invalid");
	if (client->pg_inv_wq == NULL)
		goto fail_wb_wq;
	client->trunc_wq = create_singlethread_workqueue("ceph-trunc");
	if (client->trunc_wq == NULL)
		goto fail_pg_inv_wq;

	/* set up mempools */
	err = -ENOMEM;
	client->wb_pagevec_pool = mempool_create_kmalloc_pool(10,
			      client->mount_args->wsize >> PAGE_CACHE_SHIFT);
	if (!client->wb_pagevec_pool)
		goto fail_trunc_wq;

	/* caps */
	client->min_caps = args->max_readdir;

	/* subsystems */
	err = ceph_monc_init(&client->monc, client);
	if (err < 0)
		goto fail_mempool;
	err = ceph_osdc_init(&client->osdc, client);
	if (err < 0)
		goto fail_monc;
	err = ceph_mdsc_init(&client->mdsc, client);
	if (err < 0)
		goto fail_osdc;
	return client;

fail_osdc:
	ceph_osdc_stop(&client->osdc);
fail_monc:
	ceph_monc_stop(&client->monc);
fail_mempool:
	mempool_destroy(client->wb_pagevec_pool);
fail_trunc_wq:
	destroy_workqueue(client->trunc_wq);
fail_pg_inv_wq:
	destroy_workqueue(client->pg_inv_wq);
fail_wb_wq:
	destroy_workqueue(client->wb_wq);
fail_bdi:
	bdi_destroy(&client->backing_dev_info);
fail:
	kfree(client);
	return ERR_PTR(err);
}

static void ceph_destroy_client(struct ceph_client *client)
{
	dout("destroy_client %p\n", client);

	/* unmount */
	ceph_mdsc_stop(&client->mdsc);
	ceph_osdc_stop(&client->osdc);

	/*
	 * make sure mds and osd connections close out before destroying
	 * the auth module, which is needed to free those connections'
	 * ceph_authorizers.
	 */
	ceph_msgr_flush();

	ceph_monc_stop(&client->monc);

	ceph_debugfs_client_cleanup(client);
	destroy_workqueue(client->wb_wq);
	destroy_workqueue(client->pg_inv_wq);
	destroy_workqueue(client->trunc_wq);

	bdi_destroy(&client->backing_dev_info);

	if (client->msgr)
		ceph_messenger_destroy(client->msgr);
	mempool_destroy(client->wb_pagevec_pool);

	destroy_mount_args(client->mount_args);

	kfree(client);
	dout("destroy_client %p done\n", client);
}

/*
 * Initially learn our fsid, or verify an fsid matches.
 */
int ceph_check_fsid(struct ceph_client *client, struct ceph_fsid *fsid)
{
	if (client->have_fsid) {
		if (ceph_fsid_compare(&client->fsid, fsid)) {
			pr_err("bad fsid, had %pU got %pU",
			       &client->fsid, fsid);
			return -1;
		}
	} else {
		pr_info("client%lld fsid %pU\n", client->monc.auth->global_id,
			fsid);
		memcpy(&client->fsid, fsid, sizeof(*fsid));
		ceph_debugfs_client_init(client);
		client->have_fsid = true;
	}
	return 0;
}

/*
 * true if we have the mon map (and have thus joined the cluster)
 */
static int have_mon_and_osd_map(struct ceph_client *client)
{
	return client->monc.monmap && client->monc.monmap->epoch &&
	       client->osdc.osdmap && client->osdc.osdmap->epoch;
}

/*
 * Bootstrap mount by opening the root directory.  Note the mount
 * @started time from caller, and time out if this takes too long.
 */
static struct dentry *open_root_dentry(struct ceph_client *client,
				       const char *path,
				       unsigned long started)
{
	struct ceph_mds_client *mdsc = &client->mdsc;
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
	req->r_timeout = client->mount_args->mount_timeout * HZ;
	req->r_args.getattr.mask = cpu_to_le32(CEPH_STAT_CAP_INODE);
	req->r_num_caps = 2;
	err = ceph_mdsc_do_request(mdsc, NULL, req);
	if (err == 0) {
		dout("open_root_inode success\n");
		if (ceph_ino(req->r_target_inode) == CEPH_INO_ROOT &&
		    client->sb->s_root == NULL)
			root = d_alloc_root(req->r_target_inode);
		else
			root = d_obtain_alias(req->r_target_inode);
		req->r_target_inode = NULL;
		dout("open_root_inode success, root dentry is %p\n", root);
	} else {
		root = ERR_PTR(err);
	}
	ceph_mdsc_put_request(req);
	return root;
}

/*
 * mount: join the ceph cluster, and open root directory.
 */
static int ceph_mount(struct ceph_client *client, struct vfsmount *mnt,
		      const char *path)
{
	struct ceph_entity_addr *myaddr = NULL;
	int err;
	unsigned long timeout = client->mount_args->mount_timeout * HZ;
	unsigned long started = jiffies;  /* note the start time */
	struct dentry *root;

	dout("mount start\n");
	mutex_lock(&client->mount_mutex);

	/* initialize the messenger */
	if (client->msgr == NULL) {
		if (ceph_test_opt(client, MYIP))
			myaddr = &client->mount_args->my_addr;
		client->msgr = ceph_messenger_create(myaddr);
		if (IS_ERR(client->msgr)) {
			err = PTR_ERR(client->msgr);
			client->msgr = NULL;
			goto out;
		}
		client->msgr->nocrc = ceph_test_opt(client, NOCRC);
	}

	/* open session, and wait for mon, mds, and osd maps */
	err = ceph_monc_open_session(&client->monc);
	if (err < 0)
		goto out;

	while (!have_mon_and_osd_map(client)) {
		err = -EIO;
		if (timeout && time_after_eq(jiffies, started + timeout))
			goto out;

		/* wait */
		dout("mount waiting for mon_map\n");
		err = wait_event_interruptible_timeout(client->auth_wq,
		       have_mon_and_osd_map(client) || (client->auth_err < 0),
		       timeout);
		if (err == -EINTR || err == -ERESTARTSYS)
			goto out;
		if (client->auth_err < 0) {
			err = client->auth_err;
			goto out;
		}
	}

	dout("mount opening root\n");
	root = open_root_dentry(client, "", started);
	if (IS_ERR(root)) {
		err = PTR_ERR(root);
		goto out;
	}
	if (client->sb->s_root)
		dput(root);
	else
		client->sb->s_root = root;

	if (path[0] == 0) {
		dget(root);
	} else {
		dout("mount opening base mountpoint\n");
		root = open_root_dentry(client, path, started);
		if (IS_ERR(root)) {
			err = PTR_ERR(root);
			dput(client->sb->s_root);
			client->sb->s_root = NULL;
			goto out;
		}
	}

	mnt->mnt_root = root;
	mnt->mnt_sb = client->sb;

	client->mount_state = CEPH_MOUNT_MOUNTED;
	dout("mount success\n");
	err = 0;

out:
	mutex_unlock(&client->mount_mutex);
	return err;
}

static int ceph_set_super(struct super_block *s, void *data)
{
	struct ceph_client *client = data;
	int ret;

	dout("set_super %p data %p\n", s, data);

	s->s_flags = client->mount_args->sb_flags;
	s->s_maxbytes = 1ULL << 40;  /* temp value until we get mdsmap */

	s->s_fs_info = client;
	client->sb = s;

	s->s_op = &ceph_super_ops;
	s->s_export_op = &ceph_export_ops;

	s->s_time_gran = 1000;  /* 1000 ns == 1 us */

	ret = set_anon_super(s, NULL);  /* what is that second arg for? */
	if (ret != 0)
		goto fail;

	return ret;

fail:
	s->s_fs_info = NULL;
	client->sb = NULL;
	return ret;
}

/*
 * share superblock if same fs AND options
 */
static int ceph_compare_super(struct super_block *sb, void *data)
{
	struct ceph_client *new = data;
	struct ceph_mount_args *args = new->mount_args;
	struct ceph_client *other = ceph_sb_to_client(sb);
	int i;

	dout("ceph_compare_super %p\n", sb);
	if (args->flags & CEPH_OPT_FSID) {
		if (ceph_fsid_compare(&args->fsid, &other->fsid)) {
			dout("fsid doesn't match\n");
			return 0;
		}
	} else {
		/* do we share (a) monitor? */
		for (i = 0; i < new->monc.monmap->num_mon; i++)
			if (ceph_monmap_contains(other->monc.monmap,
					 &new->monc.monmap->mon_inst[i].addr))
				break;
		if (i == new->monc.monmap->num_mon) {
			dout("mon ip not part of monmap\n");
			return 0;
		}
		dout("mon ip matches existing sb %p\n", sb);
	}
	if (args->sb_flags != other->mount_args->sb_flags) {
		dout("flags differ\n");
		return 0;
	}
	return 1;
}

/*
 * construct our own bdi so we can control readahead, etc.
 */
static atomic_long_t bdi_seq = ATOMIC_LONG_INIT(0);

static int ceph_register_bdi(struct super_block *sb, struct ceph_client *client)
{
	int err;

	/* set ra_pages based on rsize mount option? */
	if (client->mount_args->rsize >= PAGE_CACHE_SIZE)
		client->backing_dev_info.ra_pages =
			(client->mount_args->rsize + PAGE_CACHE_SIZE - 1)
			>> PAGE_SHIFT;
	err = bdi_register(&client->backing_dev_info, NULL, "ceph-%d",
			   atomic_long_inc_return(&bdi_seq));
	if (!err)
		sb->s_bdi = &client->backing_dev_info;
	return err;
}

static int ceph_get_sb(struct file_system_type *fs_type,
		       int flags, const char *dev_name, void *data,
		       struct vfsmount *mnt)
{
	struct super_block *sb;
	struct ceph_client *client;
	int err;
	int (*compare_super)(struct super_block *, void *) = ceph_compare_super;
	const char *path = NULL;
	struct ceph_mount_args *args;

	dout("ceph_get_sb\n");
	args = parse_mount_args(flags, data, dev_name, &path);
	if (IS_ERR(args)) {
		err = PTR_ERR(args);
		goto out_final;
	}

	/* create client (which we may/may not use) */
	client = ceph_create_client(args);
	if (IS_ERR(client)) {
		err = PTR_ERR(client);
		goto out_final;
	}

	if (client->mount_args->flags & CEPH_OPT_NOSHARE)
		compare_super = NULL;
	sb = sget(fs_type, compare_super, ceph_set_super, client);
	if (IS_ERR(sb)) {
		err = PTR_ERR(sb);
		goto out;
	}

	if (ceph_sb_to_client(sb) != client) {
		ceph_destroy_client(client);
		client = ceph_sb_to_client(sb);
		dout("get_sb got existing client %p\n", client);
	} else {
		dout("get_sb using new client %p\n", client);
		err = ceph_register_bdi(sb, client);
		if (err < 0)
			goto out_splat;
	}

	err = ceph_mount(client, mnt, path);
	if (err < 0)
		goto out_splat;
	dout("root %p inode %p ino %llx.%llx\n", mnt->mnt_root,
	     mnt->mnt_root->d_inode, ceph_vinop(mnt->mnt_root->d_inode));
	return 0;

out_splat:
	ceph_mdsc_close_sessions(&client->mdsc);
	deactivate_locked_super(sb);
	goto out_final;

out:
	ceph_destroy_client(client);
out_final:
	dout("ceph_get_sb fail %d\n", err);
	return err;
}

static void ceph_kill_sb(struct super_block *s)
{
	struct ceph_client *client = ceph_sb_to_client(s);
	dout("kill_sb %p\n", s);
	ceph_mdsc_pre_umount(&client->mdsc);
	kill_anon_super(s);    /* will call put_super after sb is r/o */
	ceph_destroy_client(client);
}

static struct file_system_type ceph_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ceph",
	.get_sb		= ceph_get_sb,
	.kill_sb	= ceph_kill_sb,
	.fs_flags	= FS_RENAME_DOES_D_MOVE,
};

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

static int __init init_ceph(void)
{
	int ret = 0;

	ret = ceph_debugfs_init();
	if (ret < 0)
		goto out;

	ret = ceph_msgr_init();
	if (ret < 0)
		goto out_debugfs;

	ret = init_caches();
	if (ret)
		goto out_msgr;

	ret = register_filesystem(&ceph_fs_type);
	if (ret)
		goto out_icache;

	pr_info("loaded (mon/mds/osd proto %d/%d/%d, osdmap %d/%d %d/%d)\n",
		CEPH_MONC_PROTOCOL, CEPH_MDSC_PROTOCOL, CEPH_OSDC_PROTOCOL,
		CEPH_OSDMAP_VERSION, CEPH_OSDMAP_VERSION_EXT,
		CEPH_OSDMAP_INC_VERSION, CEPH_OSDMAP_INC_VERSION_EXT);
	return 0;

out_icache:
	destroy_caches();
out_msgr:
	ceph_msgr_exit();
out_debugfs:
	ceph_debugfs_cleanup();
out:
	return ret;
}

static void __exit exit_ceph(void)
{
	dout("exit_ceph\n");
	unregister_filesystem(&ceph_fs_type);
	destroy_caches();
	ceph_msgr_exit();
	ceph_debugfs_cleanup();
}

module_init(init_ceph);
module_exit(exit_ceph);

MODULE_AUTHOR("Sage Weil <sage@newdream.net>");
MODULE_AUTHOR("Yehuda Sadeh <yehuda@hq.newdream.net>");
MODULE_AUTHOR("Patience Warnick <patience@newdream.net>");
MODULE_DESCRIPTION("Ceph filesystem for Linux");
MODULE_LICENSE("GPL");
