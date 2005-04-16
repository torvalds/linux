/* proc.c: proc files for key database enumeration
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/errno.h>
#include "internal.h"

#ifdef CONFIG_KEYS_DEBUG_PROC_KEYS
static int proc_keys_open(struct inode *inode, struct file *file);
static void *proc_keys_start(struct seq_file *p, loff_t *_pos);
static void *proc_keys_next(struct seq_file *p, void *v, loff_t *_pos);
static void proc_keys_stop(struct seq_file *p, void *v);
static int proc_keys_show(struct seq_file *m, void *v);

static struct seq_operations proc_keys_ops = {
	.start	= proc_keys_start,
	.next	= proc_keys_next,
	.stop	= proc_keys_stop,
	.show	= proc_keys_show,
};

static struct file_operations proc_keys_fops = {
	.open		= proc_keys_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};
#endif

static int proc_key_users_open(struct inode *inode, struct file *file);
static void *proc_key_users_start(struct seq_file *p, loff_t *_pos);
static void *proc_key_users_next(struct seq_file *p, void *v, loff_t *_pos);
static void proc_key_users_stop(struct seq_file *p, void *v);
static int proc_key_users_show(struct seq_file *m, void *v);

static struct seq_operations proc_key_users_ops = {
	.start	= proc_key_users_start,
	.next	= proc_key_users_next,
	.stop	= proc_key_users_stop,
	.show	= proc_key_users_show,
};

static struct file_operations proc_key_users_fops = {
	.open		= proc_key_users_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

/*****************************************************************************/
/*
 * declare the /proc files
 */
static int __init key_proc_init(void)
{
	struct proc_dir_entry *p;

#ifdef CONFIG_KEYS_DEBUG_PROC_KEYS
	p = create_proc_entry("keys", 0, NULL);
	if (!p)
		panic("Cannot create /proc/keys\n");

	p->proc_fops = &proc_keys_fops;
#endif

	p = create_proc_entry("key-users", 0, NULL);
	if (!p)
		panic("Cannot create /proc/key-users\n");

	p->proc_fops = &proc_key_users_fops;

	return 0;

} /* end key_proc_init() */

__initcall(key_proc_init);

/*****************************************************************************/
/*
 * implement "/proc/keys" to provides a list of the keys on the system
 */
#ifdef CONFIG_KEYS_DEBUG_PROC_KEYS

static int proc_keys_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &proc_keys_ops);

}

static void *proc_keys_start(struct seq_file *p, loff_t *_pos)
{
	struct rb_node *_p;
	loff_t pos = *_pos;

	spin_lock(&key_serial_lock);

	_p = rb_first(&key_serial_tree);
	while (pos > 0 && _p) {
		pos--;
		_p = rb_next(_p);
	}

	return _p;

}

static void *proc_keys_next(struct seq_file *p, void *v, loff_t *_pos)
{
	(*_pos)++;
	return rb_next((struct rb_node *) v);

}

static void proc_keys_stop(struct seq_file *p, void *v)
{
	spin_unlock(&key_serial_lock);
}

static int proc_keys_show(struct seq_file *m, void *v)
{
	struct rb_node *_p = v;
	struct key *key = rb_entry(_p, struct key, serial_node);
	struct timespec now;
	unsigned long timo;
	char xbuf[12];

	now = current_kernel_time();

	read_lock(&key->lock);

	/* come up with a suitable timeout value */
	if (key->expiry == 0) {
		memcpy(xbuf, "perm", 5);
	}
	else if (now.tv_sec >= key->expiry) {
		memcpy(xbuf, "expd", 5);
	}
	else {
		timo = key->expiry - now.tv_sec;

		if (timo < 60)
			sprintf(xbuf, "%lus", timo);
		else if (timo < 60*60)
			sprintf(xbuf, "%lum", timo / 60);
		else if (timo < 60*60*24)
			sprintf(xbuf, "%luh", timo / (60*60));
		else if (timo < 60*60*24*7)
			sprintf(xbuf, "%lud", timo / (60*60*24));
		else
			sprintf(xbuf, "%luw", timo / (60*60*24*7));
	}

	seq_printf(m, "%08x %c%c%c%c%c%c %5d %4s %06x %5d %5d %-9.9s ",
		   key->serial,
		   key->flags & KEY_FLAG_INSTANTIATED	? 'I' : '-',
		   key->flags & KEY_FLAG_REVOKED	? 'R' : '-',
		   key->flags & KEY_FLAG_DEAD		? 'D' : '-',
		   key->flags & KEY_FLAG_IN_QUOTA	? 'Q' : '-',
		   key->flags & KEY_FLAG_USER_CONSTRUCT	? 'U' : '-',
		   key->flags & KEY_FLAG_NEGATIVE	? 'N' : '-',
		   atomic_read(&key->usage),
		   xbuf,
		   key->perm,
		   key->uid,
		   key->gid,
		   key->type->name);

	if (key->type->describe)
		key->type->describe(key, m);
	seq_putc(m, '\n');

	read_unlock(&key->lock);

	return 0;

}

#endif /* CONFIG_KEYS_DEBUG_PROC_KEYS */

/*****************************************************************************/
/*
 * implement "/proc/key-users" to provides a list of the key users
 */
static int proc_key_users_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &proc_key_users_ops);

}

static void *proc_key_users_start(struct seq_file *p, loff_t *_pos)
{
	struct rb_node *_p;
	loff_t pos = *_pos;

	spin_lock(&key_user_lock);

	_p = rb_first(&key_user_tree);
	while (pos > 0 && _p) {
		pos--;
		_p = rb_next(_p);
	}

	return _p;

}

static void *proc_key_users_next(struct seq_file *p, void *v, loff_t *_pos)
{
	(*_pos)++;
	return rb_next((struct rb_node *) v);

}

static void proc_key_users_stop(struct seq_file *p, void *v)
{
	spin_unlock(&key_user_lock);
}

static int proc_key_users_show(struct seq_file *m, void *v)
{
	struct rb_node *_p = v;
	struct key_user *user = rb_entry(_p, struct key_user, node);

	seq_printf(m, "%5u: %5d %d/%d %d/%d %d/%d\n",
		   user->uid,
		   atomic_read(&user->usage),
		   atomic_read(&user->nkeys),
		   atomic_read(&user->nikeys),
		   user->qnkeys,
		   KEYQUOTA_MAX_KEYS,
		   user->qnbytes,
		   KEYQUOTA_MAX_BYTES
		   );

	return 0;

}
