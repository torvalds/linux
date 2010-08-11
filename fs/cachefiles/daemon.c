/* Daemon interface
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/namei.h>
#include <linux/poll.h>
#include <linux/mount.h>
#include <linux/statfs.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/fs_struct.h>
#include "internal.h"

static int cachefiles_daemon_open(struct inode *, struct file *);
static int cachefiles_daemon_release(struct inode *, struct file *);
static ssize_t cachefiles_daemon_read(struct file *, char __user *, size_t,
				      loff_t *);
static ssize_t cachefiles_daemon_write(struct file *, const char __user *,
				       size_t, loff_t *);
static unsigned int cachefiles_daemon_poll(struct file *,
					   struct poll_table_struct *);
static int cachefiles_daemon_frun(struct cachefiles_cache *, char *);
static int cachefiles_daemon_fcull(struct cachefiles_cache *, char *);
static int cachefiles_daemon_fstop(struct cachefiles_cache *, char *);
static int cachefiles_daemon_brun(struct cachefiles_cache *, char *);
static int cachefiles_daemon_bcull(struct cachefiles_cache *, char *);
static int cachefiles_daemon_bstop(struct cachefiles_cache *, char *);
static int cachefiles_daemon_cull(struct cachefiles_cache *, char *);
static int cachefiles_daemon_debug(struct cachefiles_cache *, char *);
static int cachefiles_daemon_dir(struct cachefiles_cache *, char *);
static int cachefiles_daemon_inuse(struct cachefiles_cache *, char *);
static int cachefiles_daemon_secctx(struct cachefiles_cache *, char *);
static int cachefiles_daemon_tag(struct cachefiles_cache *, char *);

static unsigned long cachefiles_open;

const struct file_operations cachefiles_daemon_fops = {
	.owner		= THIS_MODULE,
	.open		= cachefiles_daemon_open,
	.release	= cachefiles_daemon_release,
	.read		= cachefiles_daemon_read,
	.write		= cachefiles_daemon_write,
	.poll		= cachefiles_daemon_poll,
};

struct cachefiles_daemon_cmd {
	char name[8];
	int (*handler)(struct cachefiles_cache *cache, char *args);
};

static const struct cachefiles_daemon_cmd cachefiles_daemon_cmds[] = {
	{ "bind",	cachefiles_daemon_bind		},
	{ "brun",	cachefiles_daemon_brun		},
	{ "bcull",	cachefiles_daemon_bcull		},
	{ "bstop",	cachefiles_daemon_bstop		},
	{ "cull",	cachefiles_daemon_cull		},
	{ "debug",	cachefiles_daemon_debug		},
	{ "dir",	cachefiles_daemon_dir		},
	{ "frun",	cachefiles_daemon_frun		},
	{ "fcull",	cachefiles_daemon_fcull		},
	{ "fstop",	cachefiles_daemon_fstop		},
	{ "inuse",	cachefiles_daemon_inuse		},
	{ "secctx",	cachefiles_daemon_secctx	},
	{ "tag",	cachefiles_daemon_tag		},
	{ "",		NULL				}
};


/*
 * do various checks
 */
static int cachefiles_daemon_open(struct inode *inode, struct file *file)
{
	struct cachefiles_cache *cache;

	_enter("");

	/* only the superuser may do this */
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/* the cachefiles device may only be open once at a time */
	if (xchg(&cachefiles_open, 1) == 1)
		return -EBUSY;

	/* allocate a cache record */
	cache = kzalloc(sizeof(struct cachefiles_cache), GFP_KERNEL);
	if (!cache) {
		cachefiles_open = 0;
		return -ENOMEM;
	}

	mutex_init(&cache->daemon_mutex);
	cache->active_nodes = RB_ROOT;
	rwlock_init(&cache->active_lock);
	init_waitqueue_head(&cache->daemon_pollwq);

	/* set default caching limits
	 * - limit at 1% free space and/or free files
	 * - cull below 5% free space and/or free files
	 * - cease culling above 7% free space and/or free files
	 */
	cache->frun_percent = 7;
	cache->fcull_percent = 5;
	cache->fstop_percent = 1;
	cache->brun_percent = 7;
	cache->bcull_percent = 5;
	cache->bstop_percent = 1;

	file->private_data = cache;
	cache->cachefilesd = file;
	return 0;
}

/*
 * release a cache
 */
static int cachefiles_daemon_release(struct inode *inode, struct file *file)
{
	struct cachefiles_cache *cache = file->private_data;

	_enter("");

	ASSERT(cache);

	set_bit(CACHEFILES_DEAD, &cache->flags);

	cachefiles_daemon_unbind(cache);

	ASSERT(!cache->active_nodes.rb_node);

	/* clean up the control file interface */
	cache->cachefilesd = NULL;
	file->private_data = NULL;
	cachefiles_open = 0;

	kfree(cache);

	_leave("");
	return 0;
}

/*
 * read the cache state
 */
static ssize_t cachefiles_daemon_read(struct file *file, char __user *_buffer,
				      size_t buflen, loff_t *pos)
{
	struct cachefiles_cache *cache = file->private_data;
	char buffer[256];
	int n;

	//_enter(",,%zu,", buflen);

	if (!test_bit(CACHEFILES_READY, &cache->flags))
		return 0;

	/* check how much space the cache has */
	cachefiles_has_space(cache, 0, 0);

	/* summarise */
	clear_bit(CACHEFILES_STATE_CHANGED, &cache->flags);

	n = snprintf(buffer, sizeof(buffer),
		     "cull=%c"
		     " frun=%llx"
		     " fcull=%llx"
		     " fstop=%llx"
		     " brun=%llx"
		     " bcull=%llx"
		     " bstop=%llx",
		     test_bit(CACHEFILES_CULLING, &cache->flags) ? '1' : '0',
		     (unsigned long long) cache->frun,
		     (unsigned long long) cache->fcull,
		     (unsigned long long) cache->fstop,
		     (unsigned long long) cache->brun,
		     (unsigned long long) cache->bcull,
		     (unsigned long long) cache->bstop
		     );

	if (n > buflen)
		return -EMSGSIZE;

	if (copy_to_user(_buffer, buffer, n) != 0)
		return -EFAULT;

	return n;
}

/*
 * command the cache
 */
static ssize_t cachefiles_daemon_write(struct file *file,
				       const char __user *_data,
				       size_t datalen,
				       loff_t *pos)
{
	const struct cachefiles_daemon_cmd *cmd;
	struct cachefiles_cache *cache = file->private_data;
	ssize_t ret;
	char *data, *args, *cp;

	//_enter(",,%zu,", datalen);

	ASSERT(cache);

	if (test_bit(CACHEFILES_DEAD, &cache->flags))
		return -EIO;

	if (datalen < 0 || datalen > PAGE_SIZE - 1)
		return -EOPNOTSUPP;

	/* drag the command string into the kernel so we can parse it */
	data = kmalloc(datalen + 1, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = -EFAULT;
	if (copy_from_user(data, _data, datalen) != 0)
		goto error;

	data[datalen] = '\0';

	ret = -EINVAL;
	if (memchr(data, '\0', datalen))
		goto error;

	/* strip any newline */
	cp = memchr(data, '\n', datalen);
	if (cp) {
		if (cp == data)
			goto error;

		*cp = '\0';
	}

	/* parse the command */
	ret = -EOPNOTSUPP;

	for (args = data; *args; args++)
		if (isspace(*args))
			break;
	if (*args) {
		if (args == data)
			goto error;
		*args = '\0';
		args = skip_spaces(++args);
	}

	/* run the appropriate command handler */
	for (cmd = cachefiles_daemon_cmds; cmd->name[0]; cmd++)
		if (strcmp(cmd->name, data) == 0)
			goto found_command;

error:
	kfree(data);
	//_leave(" = %zd", ret);
	return ret;

found_command:
	mutex_lock(&cache->daemon_mutex);

	ret = -EIO;
	if (!test_bit(CACHEFILES_DEAD, &cache->flags))
		ret = cmd->handler(cache, args);

	mutex_unlock(&cache->daemon_mutex);

	if (ret == 0)
		ret = datalen;
	goto error;
}

/*
 * poll for culling state
 * - use POLLOUT to indicate culling state
 */
static unsigned int cachefiles_daemon_poll(struct file *file,
					   struct poll_table_struct *poll)
{
	struct cachefiles_cache *cache = file->private_data;
	unsigned int mask;

	poll_wait(file, &cache->daemon_pollwq, poll);
	mask = 0;

	if (test_bit(CACHEFILES_STATE_CHANGED, &cache->flags))
		mask |= POLLIN;

	if (test_bit(CACHEFILES_CULLING, &cache->flags))
		mask |= POLLOUT;

	return mask;
}

/*
 * give a range error for cache space constraints
 * - can be tail-called
 */
static int cachefiles_daemon_range_error(struct cachefiles_cache *cache,
					 char *args)
{
	kerror("Free space limits must be in range"
	       " 0%%<=stop<cull<run<100%%");

	return -EINVAL;
}

/*
 * set the percentage of files at which to stop culling
 * - command: "frun <N>%"
 */
static int cachefiles_daemon_frun(struct cachefiles_cache *cache, char *args)
{
	unsigned long frun;

	_enter(",%s", args);

	if (!*args)
		return -EINVAL;

	frun = simple_strtoul(args, &args, 10);
	if (args[0] != '%' || args[1] != '\0')
		return -EINVAL;

	if (frun <= cache->fcull_percent || frun >= 100)
		return cachefiles_daemon_range_error(cache, args);

	cache->frun_percent = frun;
	return 0;
}

/*
 * set the percentage of files at which to start culling
 * - command: "fcull <N>%"
 */
static int cachefiles_daemon_fcull(struct cachefiles_cache *cache, char *args)
{
	unsigned long fcull;

	_enter(",%s", args);

	if (!*args)
		return -EINVAL;

	fcull = simple_strtoul(args, &args, 10);
	if (args[0] != '%' || args[1] != '\0')
		return -EINVAL;

	if (fcull <= cache->fstop_percent || fcull >= cache->frun_percent)
		return cachefiles_daemon_range_error(cache, args);

	cache->fcull_percent = fcull;
	return 0;
}

/*
 * set the percentage of files at which to stop allocating
 * - command: "fstop <N>%"
 */
static int cachefiles_daemon_fstop(struct cachefiles_cache *cache, char *args)
{
	unsigned long fstop;

	_enter(",%s", args);

	if (!*args)
		return -EINVAL;

	fstop = simple_strtoul(args, &args, 10);
	if (args[0] != '%' || args[1] != '\0')
		return -EINVAL;

	if (fstop < 0 || fstop >= cache->fcull_percent)
		return cachefiles_daemon_range_error(cache, args);

	cache->fstop_percent = fstop;
	return 0;
}

/*
 * set the percentage of blocks at which to stop culling
 * - command: "brun <N>%"
 */
static int cachefiles_daemon_brun(struct cachefiles_cache *cache, char *args)
{
	unsigned long brun;

	_enter(",%s", args);

	if (!*args)
		return -EINVAL;

	brun = simple_strtoul(args, &args, 10);
	if (args[0] != '%' || args[1] != '\0')
		return -EINVAL;

	if (brun <= cache->bcull_percent || brun >= 100)
		return cachefiles_daemon_range_error(cache, args);

	cache->brun_percent = brun;
	return 0;
}

/*
 * set the percentage of blocks at which to start culling
 * - command: "bcull <N>%"
 */
static int cachefiles_daemon_bcull(struct cachefiles_cache *cache, char *args)
{
	unsigned long bcull;

	_enter(",%s", args);

	if (!*args)
		return -EINVAL;

	bcull = simple_strtoul(args, &args, 10);
	if (args[0] != '%' || args[1] != '\0')
		return -EINVAL;

	if (bcull <= cache->bstop_percent || bcull >= cache->brun_percent)
		return cachefiles_daemon_range_error(cache, args);

	cache->bcull_percent = bcull;
	return 0;
}

/*
 * set the percentage of blocks at which to stop allocating
 * - command: "bstop <N>%"
 */
static int cachefiles_daemon_bstop(struct cachefiles_cache *cache, char *args)
{
	unsigned long bstop;

	_enter(",%s", args);

	if (!*args)
		return -EINVAL;

	bstop = simple_strtoul(args, &args, 10);
	if (args[0] != '%' || args[1] != '\0')
		return -EINVAL;

	if (bstop < 0 || bstop >= cache->bcull_percent)
		return cachefiles_daemon_range_error(cache, args);

	cache->bstop_percent = bstop;
	return 0;
}

/*
 * set the cache directory
 * - command: "dir <name>"
 */
static int cachefiles_daemon_dir(struct cachefiles_cache *cache, char *args)
{
	char *dir;

	_enter(",%s", args);

	if (!*args) {
		kerror("Empty directory specified");
		return -EINVAL;
	}

	if (cache->rootdirname) {
		kerror("Second cache directory specified");
		return -EEXIST;
	}

	dir = kstrdup(args, GFP_KERNEL);
	if (!dir)
		return -ENOMEM;

	cache->rootdirname = dir;
	return 0;
}

/*
 * set the cache security context
 * - command: "secctx <ctx>"
 */
static int cachefiles_daemon_secctx(struct cachefiles_cache *cache, char *args)
{
	char *secctx;

	_enter(",%s", args);

	if (!*args) {
		kerror("Empty security context specified");
		return -EINVAL;
	}

	if (cache->secctx) {
		kerror("Second security context specified");
		return -EINVAL;
	}

	secctx = kstrdup(args, GFP_KERNEL);
	if (!secctx)
		return -ENOMEM;

	cache->secctx = secctx;
	return 0;
}

/*
 * set the cache tag
 * - command: "tag <name>"
 */
static int cachefiles_daemon_tag(struct cachefiles_cache *cache, char *args)
{
	char *tag;

	_enter(",%s", args);

	if (!*args) {
		kerror("Empty tag specified");
		return -EINVAL;
	}

	if (cache->tag)
		return -EEXIST;

	tag = kstrdup(args, GFP_KERNEL);
	if (!tag)
		return -ENOMEM;

	cache->tag = tag;
	return 0;
}

/*
 * request a node in the cache be culled from the current working directory
 * - command: "cull <name>"
 */
static int cachefiles_daemon_cull(struct cachefiles_cache *cache, char *args)
{
	struct fs_struct *fs;
	struct dentry *dir;
	const struct cred *saved_cred;
	int ret;

	_enter(",%s", args);

	if (strchr(args, '/'))
		goto inval;

	if (!test_bit(CACHEFILES_READY, &cache->flags)) {
		kerror("cull applied to unready cache");
		return -EIO;
	}

	if (test_bit(CACHEFILES_DEAD, &cache->flags)) {
		kerror("cull applied to dead cache");
		return -EIO;
	}

	/* extract the directory dentry from the cwd */
	fs = current->fs;
	read_lock(&fs->lock);
	dir = dget(fs->pwd.dentry);
	read_unlock(&fs->lock);

	if (!S_ISDIR(dir->d_inode->i_mode))
		goto notdir;

	cachefiles_begin_secure(cache, &saved_cred);
	ret = cachefiles_cull(cache, dir, args);
	cachefiles_end_secure(cache, saved_cred);

	dput(dir);
	_leave(" = %d", ret);
	return ret;

notdir:
	dput(dir);
	kerror("cull command requires dirfd to be a directory");
	return -ENOTDIR;

inval:
	kerror("cull command requires dirfd and filename");
	return -EINVAL;
}

/*
 * set debugging mode
 * - command: "debug <mask>"
 */
static int cachefiles_daemon_debug(struct cachefiles_cache *cache, char *args)
{
	unsigned long mask;

	_enter(",%s", args);

	mask = simple_strtoul(args, &args, 0);
	if (args[0] != '\0')
		goto inval;

	cachefiles_debug = mask;
	_leave(" = 0");
	return 0;

inval:
	kerror("debug command requires mask");
	return -EINVAL;
}

/*
 * find out whether an object in the current working directory is in use or not
 * - command: "inuse <name>"
 */
static int cachefiles_daemon_inuse(struct cachefiles_cache *cache, char *args)
{
	struct fs_struct *fs;
	struct dentry *dir;
	const struct cred *saved_cred;
	int ret;

	//_enter(",%s", args);

	if (strchr(args, '/'))
		goto inval;

	if (!test_bit(CACHEFILES_READY, &cache->flags)) {
		kerror("inuse applied to unready cache");
		return -EIO;
	}

	if (test_bit(CACHEFILES_DEAD, &cache->flags)) {
		kerror("inuse applied to dead cache");
		return -EIO;
	}

	/* extract the directory dentry from the cwd */
	fs = current->fs;
	read_lock(&fs->lock);
	dir = dget(fs->pwd.dentry);
	read_unlock(&fs->lock);

	if (!S_ISDIR(dir->d_inode->i_mode))
		goto notdir;

	cachefiles_begin_secure(cache, &saved_cred);
	ret = cachefiles_check_in_use(cache, dir, args);
	cachefiles_end_secure(cache, saved_cred);

	dput(dir);
	//_leave(" = %d", ret);
	return ret;

notdir:
	dput(dir);
	kerror("inuse command requires dirfd to be a directory");
	return -ENOTDIR;

inval:
	kerror("inuse command requires dirfd and filename");
	return -EINVAL;
}

/*
 * see if we have space for a number of pages and/or a number of files in the
 * cache
 */
int cachefiles_has_space(struct cachefiles_cache *cache,
			 unsigned fnr, unsigned bnr)
{
	struct kstatfs stats;
	struct path path = {
		.mnt	= cache->mnt,
		.dentry	= cache->mnt->mnt_root,
	};
	int ret;

	//_enter("{%llu,%llu,%llu,%llu,%llu,%llu},%u,%u",
	//       (unsigned long long) cache->frun,
	//       (unsigned long long) cache->fcull,
	//       (unsigned long long) cache->fstop,
	//       (unsigned long long) cache->brun,
	//       (unsigned long long) cache->bcull,
	//       (unsigned long long) cache->bstop,
	//       fnr, bnr);

	/* find out how many pages of blockdev are available */
	memset(&stats, 0, sizeof(stats));

	ret = vfs_statfs(&path, &stats);
	if (ret < 0) {
		if (ret == -EIO)
			cachefiles_io_error(cache, "statfs failed");
		_leave(" = %d", ret);
		return ret;
	}

	stats.f_bavail >>= cache->bshift;

	//_debug("avail %llu,%llu",
	//       (unsigned long long) stats.f_ffree,
	//       (unsigned long long) stats.f_bavail);

	/* see if there is sufficient space */
	if (stats.f_ffree > fnr)
		stats.f_ffree -= fnr;
	else
		stats.f_ffree = 0;

	if (stats.f_bavail > bnr)
		stats.f_bavail -= bnr;
	else
		stats.f_bavail = 0;

	ret = -ENOBUFS;
	if (stats.f_ffree < cache->fstop ||
	    stats.f_bavail < cache->bstop)
		goto begin_cull;

	ret = 0;
	if (stats.f_ffree < cache->fcull ||
	    stats.f_bavail < cache->bcull)
		goto begin_cull;

	if (test_bit(CACHEFILES_CULLING, &cache->flags) &&
	    stats.f_ffree >= cache->frun &&
	    stats.f_bavail >= cache->brun &&
	    test_and_clear_bit(CACHEFILES_CULLING, &cache->flags)
	    ) {
		_debug("cease culling");
		cachefiles_state_changed(cache);
	}

	//_leave(" = 0");
	return 0;

begin_cull:
	if (!test_and_set_bit(CACHEFILES_CULLING, &cache->flags)) {
		_debug("### CULL CACHE ###");
		cachefiles_state_changed(cache);
	}

	_leave(" = %d", ret);
	return ret;
}
