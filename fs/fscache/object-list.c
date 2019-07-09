// SPDX-License-Identifier: GPL-2.0-or-later
/* Global fscache object list maintainer and viewer
 *
 * Copyright (C) 2009 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define FSCACHE_DEBUG_LEVEL COOKIE
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/key.h>
#include <keys/user-type.h>
#include "internal.h"

static struct rb_root fscache_object_list;
static DEFINE_RWLOCK(fscache_object_list_lock);

struct fscache_objlist_data {
	unsigned long	config;		/* display configuration */
#define FSCACHE_OBJLIST_CONFIG_KEY	0x00000001	/* show object keys */
#define FSCACHE_OBJLIST_CONFIG_AUX	0x00000002	/* show object auxdata */
#define FSCACHE_OBJLIST_CONFIG_COOKIE	0x00000004	/* show objects with cookies */
#define FSCACHE_OBJLIST_CONFIG_NOCOOKIE	0x00000008	/* show objects without cookies */
#define FSCACHE_OBJLIST_CONFIG_BUSY	0x00000010	/* show busy objects */
#define FSCACHE_OBJLIST_CONFIG_IDLE	0x00000020	/* show idle objects */
#define FSCACHE_OBJLIST_CONFIG_PENDWR	0x00000040	/* show objects with pending writes */
#define FSCACHE_OBJLIST_CONFIG_NOPENDWR	0x00000080	/* show objects without pending writes */
#define FSCACHE_OBJLIST_CONFIG_READS	0x00000100	/* show objects with active reads */
#define FSCACHE_OBJLIST_CONFIG_NOREADS	0x00000200	/* show objects without active reads */
#define FSCACHE_OBJLIST_CONFIG_EVENTS	0x00000400	/* show objects with events */
#define FSCACHE_OBJLIST_CONFIG_NOEVENTS	0x00000800	/* show objects without no events */
#define FSCACHE_OBJLIST_CONFIG_WORK	0x00001000	/* show objects with work */
#define FSCACHE_OBJLIST_CONFIG_NOWORK	0x00002000	/* show objects without work */
};

/*
 * Add an object to the object list
 * - we use the address of the fscache_object structure as the key into the
 *   tree
 */
void fscache_objlist_add(struct fscache_object *obj)
{
	struct fscache_object *xobj;
	struct rb_node **p = &fscache_object_list.rb_node, *parent = NULL;

	ASSERT(RB_EMPTY_NODE(&obj->objlist_link));

	write_lock(&fscache_object_list_lock);

	while (*p) {
		parent = *p;
		xobj = rb_entry(parent, struct fscache_object, objlist_link);

		if (obj < xobj)
			p = &(*p)->rb_left;
		else if (obj > xobj)
			p = &(*p)->rb_right;
		else
			BUG();
	}

	rb_link_node(&obj->objlist_link, parent, p);
	rb_insert_color(&obj->objlist_link, &fscache_object_list);

	write_unlock(&fscache_object_list_lock);
}

/*
 * Remove an object from the object list.
 */
void fscache_objlist_remove(struct fscache_object *obj)
{
	if (RB_EMPTY_NODE(&obj->objlist_link))
		return;

	write_lock(&fscache_object_list_lock);

	BUG_ON(RB_EMPTY_ROOT(&fscache_object_list));
	rb_erase(&obj->objlist_link, &fscache_object_list);

	write_unlock(&fscache_object_list_lock);
}

/*
 * find the object in the tree on or after the specified index
 */
static struct fscache_object *fscache_objlist_lookup(loff_t *_pos)
{
	struct fscache_object *pobj, *obj = NULL, *minobj = NULL;
	struct rb_node *p;
	unsigned long pos;

	if (*_pos >= (unsigned long) ERR_PTR(-ENOENT))
		return NULL;
	pos = *_pos;

	/* banners (can't represent line 0 by pos 0 as that would involve
	 * returning a NULL pointer) */
	if (pos == 0)
		return (struct fscache_object *)(long)++(*_pos);
	if (pos < 3)
		return (struct fscache_object *)pos;

	pobj = (struct fscache_object *)pos;
	p = fscache_object_list.rb_node;
	while (p) {
		obj = rb_entry(p, struct fscache_object, objlist_link);
		if (pobj < obj) {
			if (!minobj || minobj > obj)
				minobj = obj;
			p = p->rb_left;
		} else if (pobj > obj) {
			p = p->rb_right;
		} else {
			minobj = obj;
			break;
		}
		obj = NULL;
	}

	if (!minobj)
		*_pos = (unsigned long) ERR_PTR(-ENOENT);
	else if (minobj != obj)
		*_pos = (unsigned long) minobj;
	return minobj;
}

/*
 * set up the iterator to start reading from the first line
 */
static void *fscache_objlist_start(struct seq_file *m, loff_t *_pos)
	__acquires(&fscache_object_list_lock)
{
	read_lock(&fscache_object_list_lock);
	return fscache_objlist_lookup(_pos);
}

/*
 * move to the next line
 */
static void *fscache_objlist_next(struct seq_file *m, void *v, loff_t *_pos)
{
	(*_pos)++;
	return fscache_objlist_lookup(_pos);
}

/*
 * clean up after reading
 */
static void fscache_objlist_stop(struct seq_file *m, void *v)
	__releases(&fscache_object_list_lock)
{
	read_unlock(&fscache_object_list_lock);
}

/*
 * display an object
 */
static int fscache_objlist_show(struct seq_file *m, void *v)
{
	struct fscache_objlist_data *data = m->private;
	struct fscache_object *obj = v;
	struct fscache_cookie *cookie;
	unsigned long config = data->config;
	char _type[3], *type;
	u8 *p;

	if ((unsigned long) v == 1) {
		seq_puts(m, "OBJECT   PARENT   STAT CHLDN OPS OOP IPR EX READS"
			 " EM EV FL S"
			 " | NETFS_COOKIE_DEF TY FL NETFS_DATA");
		if (config & (FSCACHE_OBJLIST_CONFIG_KEY |
			      FSCACHE_OBJLIST_CONFIG_AUX))
			seq_puts(m, "       ");
		if (config & FSCACHE_OBJLIST_CONFIG_KEY)
			seq_puts(m, "OBJECT_KEY");
		if ((config & (FSCACHE_OBJLIST_CONFIG_KEY |
			       FSCACHE_OBJLIST_CONFIG_AUX)) ==
		    (FSCACHE_OBJLIST_CONFIG_KEY | FSCACHE_OBJLIST_CONFIG_AUX))
			seq_puts(m, ", ");
		if (config & FSCACHE_OBJLIST_CONFIG_AUX)
			seq_puts(m, "AUX_DATA");
		seq_puts(m, "\n");
		return 0;
	}

	if ((unsigned long) v == 2) {
		seq_puts(m, "======== ======== ==== ===== === === === == ====="
			 " == == == ="
			 " | ================ == == ================");
		if (config & (FSCACHE_OBJLIST_CONFIG_KEY |
			      FSCACHE_OBJLIST_CONFIG_AUX))
			seq_puts(m, " ================");
		seq_puts(m, "\n");
		return 0;
	}

	/* filter out any unwanted objects */
#define FILTER(criterion, _yes, _no)					\
	do {								\
		unsigned long yes = FSCACHE_OBJLIST_CONFIG_##_yes;	\
		unsigned long no = FSCACHE_OBJLIST_CONFIG_##_no;	\
		if (criterion) {					\
			if (!(config & yes))				\
				return 0;				\
		} else {						\
			if (!(config & no))				\
				return 0;				\
		}							\
	} while(0)

	cookie = obj->cookie;
	if (~config) {
		FILTER(cookie->def,
		       COOKIE, NOCOOKIE);
		FILTER(fscache_object_is_active(obj) ||
		       obj->n_ops != 0 ||
		       obj->n_obj_ops != 0 ||
		       obj->flags ||
		       !list_empty(&obj->dependents),
		       BUSY, IDLE);
		FILTER(test_bit(FSCACHE_OBJECT_PENDING_WRITE, &obj->flags),
		       PENDWR, NOPENDWR);
		FILTER(atomic_read(&obj->n_reads),
		       READS, NOREADS);
		FILTER(obj->events & obj->event_mask,
		       EVENTS, NOEVENTS);
		FILTER(work_busy(&obj->work), WORK, NOWORK);
	}

	seq_printf(m,
		   "%8x %8x %s %5u %3u %3u %3u %2u %5u %2lx %2lx %2lx %1x | ",
		   obj->debug_id,
		   obj->parent ? obj->parent->debug_id : -1,
		   obj->state->short_name,
		   obj->n_children,
		   obj->n_ops,
		   obj->n_obj_ops,
		   obj->n_in_progress,
		   obj->n_exclusive,
		   atomic_read(&obj->n_reads),
		   obj->event_mask,
		   obj->events,
		   obj->flags,
		   work_busy(&obj->work));

	if (fscache_use_cookie(obj)) {
		uint16_t keylen = 0, auxlen = 0;

		switch (cookie->type) {
		case 0:
			type = "IX";
			break;
		case 1:
			type = "DT";
			break;
		default:
			snprintf(_type, sizeof(_type), "%02u",
				 cookie->type);
			type = _type;
			break;
		}

		seq_printf(m, "%-16s %s %2lx %16p",
			   cookie->def->name,
			   type,
			   cookie->flags,
			   cookie->netfs_data);

		if (config & FSCACHE_OBJLIST_CONFIG_KEY)
			keylen = cookie->key_len;

		if (config & FSCACHE_OBJLIST_CONFIG_AUX)
			auxlen = cookie->aux_len;

		if (keylen > 0 || auxlen > 0) {
			seq_puts(m, " ");
			p = keylen <= sizeof(cookie->inline_key) ?
				cookie->inline_key : cookie->key;
			for (; keylen > 0; keylen--)
				seq_printf(m, "%02x", *p++);
			if (auxlen > 0) {
				if (config & FSCACHE_OBJLIST_CONFIG_KEY)
					seq_puts(m, ", ");
				p = auxlen <= sizeof(cookie->inline_aux) ?
					cookie->inline_aux : cookie->aux;
				for (; auxlen > 0; auxlen--)
					seq_printf(m, "%02x", *p++);
			}
		}

		seq_puts(m, "\n");
		fscache_unuse_cookie(obj);
	} else {
		seq_puts(m, "<no_netfs>\n");
	}
	return 0;
}

static const struct seq_operations fscache_objlist_ops = {
	.start		= fscache_objlist_start,
	.stop		= fscache_objlist_stop,
	.next		= fscache_objlist_next,
	.show		= fscache_objlist_show,
};

/*
 * get the configuration for filtering the list
 */
static void fscache_objlist_config(struct fscache_objlist_data *data)
{
#ifdef CONFIG_KEYS
	const struct user_key_payload *confkey;
	unsigned long config;
	struct key *key;
	const char *buf;
	int len;

	key = request_key(&key_type_user, "fscache:objlist", NULL, NULL);
	if (IS_ERR(key))
		goto no_config;

	config = 0;
	rcu_read_lock();

	confkey = user_key_payload_rcu(key);
	if (!confkey) {
		/* key was revoked */
		rcu_read_unlock();
		key_put(key);
		goto no_config;
	}

	buf = confkey->data;

	for (len = confkey->datalen - 1; len >= 0; len--) {
		switch (buf[len]) {
		case 'K': config |= FSCACHE_OBJLIST_CONFIG_KEY;		break;
		case 'A': config |= FSCACHE_OBJLIST_CONFIG_AUX;		break;
		case 'C': config |= FSCACHE_OBJLIST_CONFIG_COOKIE;	break;
		case 'c': config |= FSCACHE_OBJLIST_CONFIG_NOCOOKIE;	break;
		case 'B': config |= FSCACHE_OBJLIST_CONFIG_BUSY;	break;
		case 'b': config |= FSCACHE_OBJLIST_CONFIG_IDLE;	break;
		case 'W': config |= FSCACHE_OBJLIST_CONFIG_PENDWR;	break;
		case 'w': config |= FSCACHE_OBJLIST_CONFIG_NOPENDWR;	break;
		case 'R': config |= FSCACHE_OBJLIST_CONFIG_READS;	break;
		case 'r': config |= FSCACHE_OBJLIST_CONFIG_NOREADS;	break;
		case 'S': config |= FSCACHE_OBJLIST_CONFIG_WORK;	break;
		case 's': config |= FSCACHE_OBJLIST_CONFIG_NOWORK;	break;
		}
	}

	rcu_read_unlock();
	key_put(key);

	if (!(config & (FSCACHE_OBJLIST_CONFIG_COOKIE | FSCACHE_OBJLIST_CONFIG_NOCOOKIE)))
	    config   |= FSCACHE_OBJLIST_CONFIG_COOKIE | FSCACHE_OBJLIST_CONFIG_NOCOOKIE;
	if (!(config & (FSCACHE_OBJLIST_CONFIG_BUSY | FSCACHE_OBJLIST_CONFIG_IDLE)))
	    config   |= FSCACHE_OBJLIST_CONFIG_BUSY | FSCACHE_OBJLIST_CONFIG_IDLE;
	if (!(config & (FSCACHE_OBJLIST_CONFIG_PENDWR | FSCACHE_OBJLIST_CONFIG_NOPENDWR)))
	    config   |= FSCACHE_OBJLIST_CONFIG_PENDWR | FSCACHE_OBJLIST_CONFIG_NOPENDWR;
	if (!(config & (FSCACHE_OBJLIST_CONFIG_READS | FSCACHE_OBJLIST_CONFIG_NOREADS)))
	    config   |= FSCACHE_OBJLIST_CONFIG_READS | FSCACHE_OBJLIST_CONFIG_NOREADS;
	if (!(config & (FSCACHE_OBJLIST_CONFIG_EVENTS | FSCACHE_OBJLIST_CONFIG_NOEVENTS)))
	    config   |= FSCACHE_OBJLIST_CONFIG_EVENTS | FSCACHE_OBJLIST_CONFIG_NOEVENTS;
	if (!(config & (FSCACHE_OBJLIST_CONFIG_WORK | FSCACHE_OBJLIST_CONFIG_NOWORK)))
	    config   |= FSCACHE_OBJLIST_CONFIG_WORK | FSCACHE_OBJLIST_CONFIG_NOWORK;

	data->config = config;
	return;

no_config:
#endif
	data->config = ULONG_MAX;
}

/*
 * open "/proc/fs/fscache/objects" to provide a list of active objects
 * - can be configured by a user-defined key added to the caller's keyrings
 */
static int fscache_objlist_open(struct inode *inode, struct file *file)
{
	struct fscache_objlist_data *data;

	data = __seq_open_private(file, &fscache_objlist_ops, sizeof(*data));
	if (!data)
		return -ENOMEM;

	/* get the configuration key */
	fscache_objlist_config(data);

	return 0;
}

/*
 * clean up on close
 */
static int fscache_objlist_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;

	kfree(m->private);
	m->private = NULL;
	return seq_release(inode, file);
}

const struct file_operations fscache_objlist_fops = {
	.open		= fscache_objlist_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= fscache_objlist_release,
};
