/* CacheFiles path walking and related routines
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
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fsnotify.h>
#include <linux/quotaops.h>
#include <linux/xattr.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/security.h>
#include <linux/slab.h>
#include "internal.h"

#define CACHEFILES_KEYBUF_SIZE 512

/*
 * dump debugging info about an object
 */
static noinline
void __cachefiles_printk_object(struct cachefiles_object *object,
				const char *prefix,
				u8 *keybuf)
{
	struct fscache_cookie *cookie;
	unsigned keylen, loop;

	printk(KERN_ERR "%sobject: OBJ%x\n",
	       prefix, object->fscache.debug_id);
	printk(KERN_ERR "%sobjstate=%s fl=%lx swfl=%lx ev=%lx[%lx]\n",
	       prefix, fscache_object_states[object->fscache.state],
	       object->fscache.flags, object->fscache.work.flags,
	       object->fscache.events,
	       object->fscache.event_mask & FSCACHE_OBJECT_EVENTS_MASK);
	printk(KERN_ERR "%sops=%u inp=%u exc=%u\n",
	       prefix, object->fscache.n_ops, object->fscache.n_in_progress,
	       object->fscache.n_exclusive);
	printk(KERN_ERR "%sparent=%p\n",
	       prefix, object->fscache.parent);

	spin_lock(&object->fscache.lock);
	cookie = object->fscache.cookie;
	if (cookie) {
		printk(KERN_ERR "%scookie=%p [pr=%p nd=%p fl=%lx]\n",
		       prefix,
		       object->fscache.cookie,
		       object->fscache.cookie->parent,
		       object->fscache.cookie->netfs_data,
		       object->fscache.cookie->flags);
		if (keybuf)
			keylen = cookie->def->get_key(cookie->netfs_data, keybuf,
						      CACHEFILES_KEYBUF_SIZE);
		else
			keylen = 0;
	} else {
		printk(KERN_ERR "%scookie=NULL\n", prefix);
		keylen = 0;
	}
	spin_unlock(&object->fscache.lock);

	if (keylen) {
		printk(KERN_ERR "%skey=[%u] '", prefix, keylen);
		for (loop = 0; loop < keylen; loop++)
			printk("%02x", keybuf[loop]);
		printk("'\n");
	}
}

/*
 * dump debugging info about a pair of objects
 */
static noinline void cachefiles_printk_object(struct cachefiles_object *object,
					      struct cachefiles_object *xobject)
{
	u8 *keybuf;

	keybuf = kmalloc(CACHEFILES_KEYBUF_SIZE, GFP_NOIO);
	if (object)
		__cachefiles_printk_object(object, "", keybuf);
	if (xobject)
		__cachefiles_printk_object(xobject, "x", keybuf);
	kfree(keybuf);
}

/*
 * mark the owner of a dentry, if there is one, to indicate that that dentry
 * has been preemptively deleted
 * - the caller must hold the i_mutex on the dentry's parent as required to
 *   call vfs_unlink(), vfs_rmdir() or vfs_rename()
 */
static void cachefiles_mark_object_buried(struct cachefiles_cache *cache,
					  struct dentry *dentry)
{
	struct cachefiles_object *object;
	struct rb_node *p;

	_enter(",'%*.*s'",
	       dentry->d_name.len, dentry->d_name.len, dentry->d_name.name);

	write_lock(&cache->active_lock);

	p = cache->active_nodes.rb_node;
	while (p) {
		object = rb_entry(p, struct cachefiles_object, active_node);
		if (object->dentry > dentry)
			p = p->rb_left;
		else if (object->dentry < dentry)
			p = p->rb_right;
		else
			goto found_dentry;
	}

	write_unlock(&cache->active_lock);
	_leave(" [no owner]");
	return;

	/* found the dentry for  */
found_dentry:
	kdebug("preemptive burial: OBJ%x [%s] %p",
	       object->fscache.debug_id,
	       fscache_object_states[object->fscache.state],
	       dentry);

	if (object->fscache.state < FSCACHE_OBJECT_DYING) {
		printk(KERN_ERR "\n");
		printk(KERN_ERR "CacheFiles: Error:"
		       " Can't preemptively bury live object\n");
		cachefiles_printk_object(object, NULL);
	} else if (test_and_set_bit(CACHEFILES_OBJECT_BURIED, &object->flags)) {
		printk(KERN_ERR "CacheFiles: Error:"
		       " Object already preemptively buried\n");
	}

	write_unlock(&cache->active_lock);
	_leave(" [owner marked]");
}

/*
 * record the fact that an object is now active
 */
static int cachefiles_mark_object_active(struct cachefiles_cache *cache,
					 struct cachefiles_object *object)
{
	struct cachefiles_object *xobject;
	struct rb_node **_p, *_parent = NULL;
	struct dentry *dentry;

	_enter(",%p", object);

try_again:
	write_lock(&cache->active_lock);

	if (test_and_set_bit(CACHEFILES_OBJECT_ACTIVE, &object->flags)) {
		printk(KERN_ERR "CacheFiles: Error: Object already active\n");
		cachefiles_printk_object(object, NULL);
		BUG();
	}

	dentry = object->dentry;
	_p = &cache->active_nodes.rb_node;
	while (*_p) {
		_parent = *_p;
		xobject = rb_entry(_parent,
				   struct cachefiles_object, active_node);

		ASSERT(xobject != object);

		if (xobject->dentry > dentry)
			_p = &(*_p)->rb_left;
		else if (xobject->dentry < dentry)
			_p = &(*_p)->rb_right;
		else
			goto wait_for_old_object;
	}

	rb_link_node(&object->active_node, _parent, _p);
	rb_insert_color(&object->active_node, &cache->active_nodes);

	write_unlock(&cache->active_lock);
	_leave(" = 0");
	return 0;

	/* an old object from a previous incarnation is hogging the slot - we
	 * need to wait for it to be destroyed */
wait_for_old_object:
	if (xobject->fscache.state < FSCACHE_OBJECT_DYING) {
		printk(KERN_ERR "\n");
		printk(KERN_ERR "CacheFiles: Error:"
		       " Unexpected object collision\n");
		cachefiles_printk_object(object, xobject);
		BUG();
	}
	atomic_inc(&xobject->usage);
	write_unlock(&cache->active_lock);

	if (test_bit(CACHEFILES_OBJECT_ACTIVE, &xobject->flags)) {
		wait_queue_head_t *wq;

		signed long timeout = 60 * HZ;
		wait_queue_t wait;
		bool requeue;

		/* if the object we're waiting for is queued for processing,
		 * then just put ourselves on the queue behind it */
		if (slow_work_is_queued(&xobject->fscache.work)) {
			_debug("queue OBJ%x behind OBJ%x immediately",
			       object->fscache.debug_id,
			       xobject->fscache.debug_id);
			goto requeue;
		}

		/* otherwise we sleep until either the object we're waiting for
		 * is done, or the slow-work facility wants the thread back to
		 * do other work */
		wq = bit_waitqueue(&xobject->flags, CACHEFILES_OBJECT_ACTIVE);
		init_wait(&wait);
		requeue = false;
		do {
			prepare_to_wait(wq, &wait, TASK_UNINTERRUPTIBLE);
			if (!test_bit(CACHEFILES_OBJECT_ACTIVE, &xobject->flags))
				break;
			requeue = slow_work_sleep_till_thread_needed(
				&object->fscache.work, &timeout);
		} while (timeout > 0 && !requeue);
		finish_wait(wq, &wait);

		if (requeue &&
		    test_bit(CACHEFILES_OBJECT_ACTIVE, &xobject->flags)) {
			_debug("queue OBJ%x behind OBJ%x after wait",
			       object->fscache.debug_id,
			       xobject->fscache.debug_id);
			goto requeue;
		}

		if (timeout <= 0) {
			printk(KERN_ERR "\n");
			printk(KERN_ERR "CacheFiles: Error: Overlong"
			       " wait for old active object to go away\n");
			cachefiles_printk_object(object, xobject);
			goto requeue;
		}
	}

	ASSERT(!test_bit(CACHEFILES_OBJECT_ACTIVE, &xobject->flags));

	cache->cache.ops->put_object(&xobject->fscache);
	goto try_again;

requeue:
	clear_bit(CACHEFILES_OBJECT_ACTIVE, &object->flags);
	cache->cache.ops->put_object(&xobject->fscache);
	_leave(" = -ETIMEDOUT");
	return -ETIMEDOUT;
}

/*
 * delete an object representation from the cache
 * - file backed objects are unlinked
 * - directory backed objects are stuffed into the graveyard for userspace to
 *   delete
 * - unlocks the directory mutex
 */
static int cachefiles_bury_object(struct cachefiles_cache *cache,
				  struct dentry *dir,
				  struct dentry *rep,
				  bool preemptive)
{
	struct dentry *grave, *trap;
	char nbuffer[8 + 8 + 1];
	int ret;

	_enter(",'%*.*s','%*.*s'",
	       dir->d_name.len, dir->d_name.len, dir->d_name.name,
	       rep->d_name.len, rep->d_name.len, rep->d_name.name);

	_debug("remove %p from %p", rep, dir);

	/* non-directories can just be unlinked */
	if (!S_ISDIR(rep->d_inode->i_mode)) {
		_debug("unlink stale object");
		ret = vfs_unlink(dir->d_inode, rep);

		if (preemptive)
			cachefiles_mark_object_buried(cache, rep);

		mutex_unlock(&dir->d_inode->i_mutex);

		if (ret == -EIO)
			cachefiles_io_error(cache, "Unlink failed");

		_leave(" = %d", ret);
		return ret;
	}

	/* directories have to be moved to the graveyard */
	_debug("move stale object to graveyard");
	mutex_unlock(&dir->d_inode->i_mutex);

try_again:
	/* first step is to make up a grave dentry in the graveyard */
	sprintf(nbuffer, "%08x%08x",
		(uint32_t) get_seconds(),
		(uint32_t) atomic_inc_return(&cache->gravecounter));

	/* do the multiway lock magic */
	trap = lock_rename(cache->graveyard, dir);

	/* do some checks before getting the grave dentry */
	if (rep->d_parent != dir) {
		/* the entry was probably culled when we dropped the parent dir
		 * lock */
		unlock_rename(cache->graveyard, dir);
		_leave(" = 0 [culled?]");
		return 0;
	}

	if (!S_ISDIR(cache->graveyard->d_inode->i_mode)) {
		unlock_rename(cache->graveyard, dir);
		cachefiles_io_error(cache, "Graveyard no longer a directory");
		return -EIO;
	}

	if (trap == rep) {
		unlock_rename(cache->graveyard, dir);
		cachefiles_io_error(cache, "May not make directory loop");
		return -EIO;
	}

	if (d_mountpoint(rep)) {
		unlock_rename(cache->graveyard, dir);
		cachefiles_io_error(cache, "Mountpoint in cache");
		return -EIO;
	}

	grave = lookup_one_len(nbuffer, cache->graveyard, strlen(nbuffer));
	if (IS_ERR(grave)) {
		unlock_rename(cache->graveyard, dir);

		if (PTR_ERR(grave) == -ENOMEM) {
			_leave(" = -ENOMEM");
			return -ENOMEM;
		}

		cachefiles_io_error(cache, "Lookup error %ld",
				    PTR_ERR(grave));
		return -EIO;
	}

	if (grave->d_inode) {
		unlock_rename(cache->graveyard, dir);
		dput(grave);
		grave = NULL;
		cond_resched();
		goto try_again;
	}

	if (d_mountpoint(grave)) {
		unlock_rename(cache->graveyard, dir);
		dput(grave);
		cachefiles_io_error(cache, "Mountpoint in graveyard");
		return -EIO;
	}

	/* target should not be an ancestor of source */
	if (trap == grave) {
		unlock_rename(cache->graveyard, dir);
		dput(grave);
		cachefiles_io_error(cache, "May not make directory loop");
		return -EIO;
	}

	/* attempt the rename */
	ret = vfs_rename(dir->d_inode, rep, cache->graveyard->d_inode, grave);
	if (ret != 0 && ret != -ENOMEM)
		cachefiles_io_error(cache, "Rename failed with error %d", ret);

	if (preemptive)
		cachefiles_mark_object_buried(cache, rep);

	unlock_rename(cache->graveyard, dir);
	dput(grave);
	_leave(" = 0");
	return 0;
}

/*
 * delete an object representation from the cache
 */
int cachefiles_delete_object(struct cachefiles_cache *cache,
			     struct cachefiles_object *object)
{
	struct dentry *dir;
	int ret;

	_enter(",OBJ%x{%p}", object->fscache.debug_id, object->dentry);

	ASSERT(object->dentry);
	ASSERT(object->dentry->d_inode);
	ASSERT(object->dentry->d_parent);

	dir = dget_parent(object->dentry);

	mutex_lock_nested(&dir->d_inode->i_mutex, I_MUTEX_PARENT);

	if (test_bit(CACHEFILES_OBJECT_BURIED, &object->flags)) {
		/* object allocation for the same key preemptively deleted this
		 * object's file so that it could create its own file */
		_debug("object preemptively buried");
		mutex_unlock(&dir->d_inode->i_mutex);
		ret = 0;
	} else {
		/* we need to check that our parent is _still_ our parent - it
		 * may have been renamed */
		if (dir == object->dentry->d_parent) {
			ret = cachefiles_bury_object(cache, dir,
						     object->dentry, false);
		} else {
			/* it got moved, presumably by cachefilesd culling it,
			 * so it's no longer in the key path and we can ignore
			 * it */
			mutex_unlock(&dir->d_inode->i_mutex);
			ret = 0;
		}
	}

	dput(dir);
	_leave(" = %d", ret);
	return ret;
}

/*
 * walk from the parent object to the child object through the backing
 * filesystem, creating directories as we go
 */
int cachefiles_walk_to_object(struct cachefiles_object *parent,
			      struct cachefiles_object *object,
			      const char *key,
			      struct cachefiles_xattr *auxdata)
{
	struct cachefiles_cache *cache;
	struct dentry *dir, *next = NULL;
	unsigned long start;
	const char *name;
	int ret, nlen;

	_enter("OBJ%x{%p},OBJ%x,%s,",
	       parent->fscache.debug_id, parent->dentry,
	       object->fscache.debug_id, key);

	cache = container_of(parent->fscache.cache,
			     struct cachefiles_cache, cache);

	ASSERT(parent->dentry);
	ASSERT(parent->dentry->d_inode);

	if (!(S_ISDIR(parent->dentry->d_inode->i_mode))) {
		// TODO: convert file to dir
		_leave("looking up in none directory");
		return -ENOBUFS;
	}

	dir = dget(parent->dentry);

advance:
	/* attempt to transit the first directory component */
	name = key;
	nlen = strlen(key);

	/* key ends in a double NUL */
	key = key + nlen + 1;
	if (!*key)
		key = NULL;

lookup_again:
	/* search the current directory for the element name */
	_debug("lookup '%s'", name);

	mutex_lock_nested(&dir->d_inode->i_mutex, I_MUTEX_PARENT);

	start = jiffies;
	next = lookup_one_len(name, dir, nlen);
	cachefiles_hist(cachefiles_lookup_histogram, start);
	if (IS_ERR(next))
		goto lookup_error;

	_debug("next -> %p %s", next, next->d_inode ? "positive" : "negative");

	if (!key)
		object->new = !next->d_inode;

	/* if this element of the path doesn't exist, then the lookup phase
	 * failed, and we can release any readers in the certain knowledge that
	 * there's nothing for them to actually read */
	if (!next->d_inode)
		fscache_object_lookup_negative(&object->fscache);

	/* we need to create the object if it's negative */
	if (key || object->type == FSCACHE_COOKIE_TYPE_INDEX) {
		/* index objects and intervening tree levels must be subdirs */
		if (!next->d_inode) {
			ret = cachefiles_has_space(cache, 1, 0);
			if (ret < 0)
				goto create_error;

			start = jiffies;
			ret = vfs_mkdir(dir->d_inode, next, 0);
			cachefiles_hist(cachefiles_mkdir_histogram, start);
			if (ret < 0)
				goto create_error;

			ASSERT(next->d_inode);

			_debug("mkdir -> %p{%p{ino=%lu}}",
			       next, next->d_inode, next->d_inode->i_ino);

		} else if (!S_ISDIR(next->d_inode->i_mode)) {
			kerror("inode %lu is not a directory",
			       next->d_inode->i_ino);
			ret = -ENOBUFS;
			goto error;
		}

	} else {
		/* non-index objects start out life as files */
		if (!next->d_inode) {
			ret = cachefiles_has_space(cache, 1, 0);
			if (ret < 0)
				goto create_error;

			start = jiffies;
			ret = vfs_create(dir->d_inode, next, S_IFREG, NULL);
			cachefiles_hist(cachefiles_create_histogram, start);
			if (ret < 0)
				goto create_error;

			ASSERT(next->d_inode);

			_debug("create -> %p{%p{ino=%lu}}",
			       next, next->d_inode, next->d_inode->i_ino);

		} else if (!S_ISDIR(next->d_inode->i_mode) &&
			   !S_ISREG(next->d_inode->i_mode)
			   ) {
			kerror("inode %lu is not a file or directory",
			       next->d_inode->i_ino);
			ret = -ENOBUFS;
			goto error;
		}
	}

	/* process the next component */
	if (key) {
		_debug("advance");
		mutex_unlock(&dir->d_inode->i_mutex);
		dput(dir);
		dir = next;
		next = NULL;
		goto advance;
	}

	/* we've found the object we were looking for */
	object->dentry = next;

	/* if we've found that the terminal object exists, then we need to
	 * check its attributes and delete it if it's out of date */
	if (!object->new) {
		_debug("validate '%*.*s'",
		       next->d_name.len, next->d_name.len, next->d_name.name);

		ret = cachefiles_check_object_xattr(object, auxdata);
		if (ret == -ESTALE) {
			/* delete the object (the deleter drops the directory
			 * mutex) */
			object->dentry = NULL;

			ret = cachefiles_bury_object(cache, dir, next, true);
			dput(next);
			next = NULL;

			if (ret < 0)
				goto delete_error;

			_debug("redo lookup");
			goto lookup_again;
		}
	}

	/* note that we're now using this object */
	ret = cachefiles_mark_object_active(cache, object);

	mutex_unlock(&dir->d_inode->i_mutex);
	dput(dir);
	dir = NULL;

	if (ret == -ETIMEDOUT)
		goto mark_active_timed_out;

	_debug("=== OBTAINED_OBJECT ===");

	if (object->new) {
		/* attach data to a newly constructed terminal object */
		ret = cachefiles_set_object_xattr(object, auxdata);
		if (ret < 0)
			goto check_error;
	} else {
		/* always update the atime on an object we've just looked up
		 * (this is used to keep track of culling, and atimes are only
		 * updated by read, write and readdir but not lookup or
		 * open) */
		touch_atime(cache->mnt, next);
	}

	/* open a file interface onto a data file */
	if (object->type != FSCACHE_COOKIE_TYPE_INDEX) {
		if (S_ISREG(object->dentry->d_inode->i_mode)) {
			const struct address_space_operations *aops;

			ret = -EPERM;
			aops = object->dentry->d_inode->i_mapping->a_ops;
			if (!aops->bmap)
				goto check_error;

			object->backer = object->dentry;
		} else {
			BUG(); // TODO: open file in data-class subdir
		}
	}

	object->new = 0;
	fscache_obtained_object(&object->fscache);

	_leave(" = 0 [%lu]", object->dentry->d_inode->i_ino);
	return 0;

create_error:
	_debug("create error %d", ret);
	if (ret == -EIO)
		cachefiles_io_error(cache, "Create/mkdir failed");
	goto error;

mark_active_timed_out:
	_debug("mark active timed out");
	goto release_dentry;

check_error:
	_debug("check error %d", ret);
	write_lock(&cache->active_lock);
	rb_erase(&object->active_node, &cache->active_nodes);
	clear_bit(CACHEFILES_OBJECT_ACTIVE, &object->flags);
	wake_up_bit(&object->flags, CACHEFILES_OBJECT_ACTIVE);
	write_unlock(&cache->active_lock);
release_dentry:
	dput(object->dentry);
	object->dentry = NULL;
	goto error_out;

delete_error:
	_debug("delete error %d", ret);
	goto error_out2;

lookup_error:
	_debug("lookup error %ld", PTR_ERR(next));
	ret = PTR_ERR(next);
	if (ret == -EIO)
		cachefiles_io_error(cache, "Lookup failed");
	next = NULL;
error:
	mutex_unlock(&dir->d_inode->i_mutex);
	dput(next);
error_out2:
	dput(dir);
error_out:
	_leave(" = error %d", -ret);
	return ret;
}

/*
 * get a subdirectory
 */
struct dentry *cachefiles_get_directory(struct cachefiles_cache *cache,
					struct dentry *dir,
					const char *dirname)
{
	struct dentry *subdir;
	unsigned long start;
	int ret;

	_enter(",,%s", dirname);

	/* search the current directory for the element name */
	mutex_lock(&dir->d_inode->i_mutex);

	start = jiffies;
	subdir = lookup_one_len(dirname, dir, strlen(dirname));
	cachefiles_hist(cachefiles_lookup_histogram, start);
	if (IS_ERR(subdir)) {
		if (PTR_ERR(subdir) == -ENOMEM)
			goto nomem_d_alloc;
		goto lookup_error;
	}

	_debug("subdir -> %p %s",
	       subdir, subdir->d_inode ? "positive" : "negative");

	/* we need to create the subdir if it doesn't exist yet */
	if (!subdir->d_inode) {
		ret = cachefiles_has_space(cache, 1, 0);
		if (ret < 0)
			goto mkdir_error;

		_debug("attempt mkdir");

		ret = vfs_mkdir(dir->d_inode, subdir, 0700);
		if (ret < 0)
			goto mkdir_error;

		ASSERT(subdir->d_inode);

		_debug("mkdir -> %p{%p{ino=%lu}}",
		       subdir,
		       subdir->d_inode,
		       subdir->d_inode->i_ino);
	}

	mutex_unlock(&dir->d_inode->i_mutex);

	/* we need to make sure the subdir is a directory */
	ASSERT(subdir->d_inode);

	if (!S_ISDIR(subdir->d_inode->i_mode)) {
		kerror("%s is not a directory", dirname);
		ret = -EIO;
		goto check_error;
	}

	ret = -EPERM;
	if (!subdir->d_inode->i_op ||
	    !subdir->d_inode->i_op->setxattr ||
	    !subdir->d_inode->i_op->getxattr ||
	    !subdir->d_inode->i_op->lookup ||
	    !subdir->d_inode->i_op->mkdir ||
	    !subdir->d_inode->i_op->create ||
	    !subdir->d_inode->i_op->rename ||
	    !subdir->d_inode->i_op->rmdir ||
	    !subdir->d_inode->i_op->unlink)
		goto check_error;

	_leave(" = [%lu]", subdir->d_inode->i_ino);
	return subdir;

check_error:
	dput(subdir);
	_leave(" = %d [check]", ret);
	return ERR_PTR(ret);

mkdir_error:
	mutex_unlock(&dir->d_inode->i_mutex);
	dput(subdir);
	kerror("mkdir %s failed with error %d", dirname, ret);
	return ERR_PTR(ret);

lookup_error:
	mutex_unlock(&dir->d_inode->i_mutex);
	ret = PTR_ERR(subdir);
	kerror("Lookup %s failed with error %d", dirname, ret);
	return ERR_PTR(ret);

nomem_d_alloc:
	mutex_unlock(&dir->d_inode->i_mutex);
	_leave(" = -ENOMEM");
	return ERR_PTR(-ENOMEM);
}

/*
 * find out if an object is in use or not
 * - if finds object and it's not in use:
 *   - returns a pointer to the object and a reference on it
 *   - returns with the directory locked
 */
static struct dentry *cachefiles_check_active(struct cachefiles_cache *cache,
					      struct dentry *dir,
					      char *filename)
{
	struct cachefiles_object *object;
	struct rb_node *_n;
	struct dentry *victim;
	unsigned long start;
	int ret;

	//_enter(",%*.*s/,%s",
	//       dir->d_name.len, dir->d_name.len, dir->d_name.name, filename);

	/* look up the victim */
	mutex_lock_nested(&dir->d_inode->i_mutex, 1);

	start = jiffies;
	victim = lookup_one_len(filename, dir, strlen(filename));
	cachefiles_hist(cachefiles_lookup_histogram, start);
	if (IS_ERR(victim))
		goto lookup_error;

	//_debug("victim -> %p %s",
	//       victim, victim->d_inode ? "positive" : "negative");

	/* if the object is no longer there then we probably retired the object
	 * at the netfs's request whilst the cull was in progress
	 */
	if (!victim->d_inode) {
		mutex_unlock(&dir->d_inode->i_mutex);
		dput(victim);
		_leave(" = -ENOENT [absent]");
		return ERR_PTR(-ENOENT);
	}

	/* check to see if we're using this object */
	read_lock(&cache->active_lock);

	_n = cache->active_nodes.rb_node;

	while (_n) {
		object = rb_entry(_n, struct cachefiles_object, active_node);

		if (object->dentry > victim)
			_n = _n->rb_left;
		else if (object->dentry < victim)
			_n = _n->rb_right;
		else
			goto object_in_use;
	}

	read_unlock(&cache->active_lock);

	//_leave(" = %p", victim);
	return victim;

object_in_use:
	read_unlock(&cache->active_lock);
	mutex_unlock(&dir->d_inode->i_mutex);
	dput(victim);
	//_leave(" = -EBUSY [in use]");
	return ERR_PTR(-EBUSY);

lookup_error:
	mutex_unlock(&dir->d_inode->i_mutex);
	ret = PTR_ERR(victim);
	if (ret == -ENOENT) {
		/* file or dir now absent - probably retired by netfs */
		_leave(" = -ESTALE [absent]");
		return ERR_PTR(-ESTALE);
	}

	if (ret == -EIO) {
		cachefiles_io_error(cache, "Lookup failed");
	} else if (ret != -ENOMEM) {
		kerror("Internal error: %d", ret);
		ret = -EIO;
	}

	_leave(" = %d", ret);
	return ERR_PTR(ret);
}

/*
 * cull an object if it's not in use
 * - called only by cache manager daemon
 */
int cachefiles_cull(struct cachefiles_cache *cache, struct dentry *dir,
		    char *filename)
{
	struct dentry *victim;
	int ret;

	_enter(",%*.*s/,%s",
	       dir->d_name.len, dir->d_name.len, dir->d_name.name, filename);

	victim = cachefiles_check_active(cache, dir, filename);
	if (IS_ERR(victim))
		return PTR_ERR(victim);

	_debug("victim -> %p %s",
	       victim, victim->d_inode ? "positive" : "negative");

	/* okay... the victim is not being used so we can cull it
	 * - start by marking it as stale
	 */
	_debug("victim is cullable");

	ret = cachefiles_remove_object_xattr(cache, victim);
	if (ret < 0)
		goto error_unlock;

	/*  actually remove the victim (drops the dir mutex) */
	_debug("bury");

	ret = cachefiles_bury_object(cache, dir, victim, false);
	if (ret < 0)
		goto error;

	dput(victim);
	_leave(" = 0");
	return 0;

error_unlock:
	mutex_unlock(&dir->d_inode->i_mutex);
error:
	dput(victim);
	if (ret == -ENOENT) {
		/* file or dir now absent - probably retired by netfs */
		_leave(" = -ESTALE [absent]");
		return -ESTALE;
	}

	if (ret != -ENOMEM) {
		kerror("Internal error: %d", ret);
		ret = -EIO;
	}

	_leave(" = %d", ret);
	return ret;
}

/*
 * find out if an object is in use or not
 * - called only by cache manager daemon
 * - returns -EBUSY or 0 to indicate whether an object is in use or not
 */
int cachefiles_check_in_use(struct cachefiles_cache *cache, struct dentry *dir,
			    char *filename)
{
	struct dentry *victim;

	//_enter(",%*.*s/,%s",
	//       dir->d_name.len, dir->d_name.len, dir->d_name.name, filename);

	victim = cachefiles_check_active(cache, dir, filename);
	if (IS_ERR(victim))
		return PTR_ERR(victim);

	mutex_unlock(&dir->d_inode->i_mutex);
	dput(victim);
	//_leave(" = 0");
	return 0;
}
