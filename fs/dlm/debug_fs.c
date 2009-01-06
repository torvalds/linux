/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2008 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>

#include "dlm_internal.h"
#include "lock.h"

#define DLM_DEBUG_BUF_LEN 4096
static char debug_buf[DLM_DEBUG_BUF_LEN];
static struct mutex debug_buf_lock;

static struct dentry *dlm_root;

struct rsb_iter {
	int entry;
	int format;
	int header;
	struct dlm_ls *ls;
	struct list_head *next;
	struct dlm_rsb *rsb;
};

/*
 * dump all rsb's in the lockspace hash table
 */

static char *print_lockmode(int mode)
{
	switch (mode) {
	case DLM_LOCK_IV:
		return "--";
	case DLM_LOCK_NL:
		return "NL";
	case DLM_LOCK_CR:
		return "CR";
	case DLM_LOCK_CW:
		return "CW";
	case DLM_LOCK_PR:
		return "PR";
	case DLM_LOCK_PW:
		return "PW";
	case DLM_LOCK_EX:
		return "EX";
	default:
		return "??";
	}
}

static void print_format1_lock(struct seq_file *s, struct dlm_lkb *lkb,
			       struct dlm_rsb *res)
{
	seq_printf(s, "%08x %s", lkb->lkb_id, print_lockmode(lkb->lkb_grmode));

	if (lkb->lkb_status == DLM_LKSTS_CONVERT
	    || lkb->lkb_status == DLM_LKSTS_WAITING)
		seq_printf(s, " (%s)", print_lockmode(lkb->lkb_rqmode));

	if (lkb->lkb_nodeid) {
		if (lkb->lkb_nodeid != res->res_nodeid)
			seq_printf(s, " Remote: %3d %08x", lkb->lkb_nodeid,
				   lkb->lkb_remid);
		else
			seq_printf(s, " Master:     %08x", lkb->lkb_remid);
	}

	if (lkb->lkb_wait_type)
		seq_printf(s, " wait_type: %d", lkb->lkb_wait_type);

	seq_printf(s, "\n");
}

static int print_format1(struct dlm_rsb *res, struct seq_file *s)
{
	struct dlm_lkb *lkb;
	int i, lvblen = res->res_ls->ls_lvblen, recover_list, root_list;

	lock_rsb(res);

	seq_printf(s, "\nResource %p Name (len=%d) \"", res, res->res_length);
	for (i = 0; i < res->res_length; i++) {
		if (isprint(res->res_name[i]))
			seq_printf(s, "%c", res->res_name[i]);
		else
			seq_printf(s, "%c", '.');
	}
	if (res->res_nodeid > 0)
		seq_printf(s, "\"  \nLocal Copy, Master is node %d\n",
			   res->res_nodeid);
	else if (res->res_nodeid == 0)
		seq_printf(s, "\"  \nMaster Copy\n");
	else if (res->res_nodeid == -1)
		seq_printf(s, "\"  \nLooking up master (lkid %x)\n",
			   res->res_first_lkid);
	else
		seq_printf(s, "\"  \nInvalid master %d\n", res->res_nodeid);

	/* Print the LVB: */
	if (res->res_lvbptr) {
		seq_printf(s, "LVB: ");
		for (i = 0; i < lvblen; i++) {
			if (i == lvblen / 2)
				seq_printf(s, "\n     ");
			seq_printf(s, "%02x ",
				   (unsigned char) res->res_lvbptr[i]);
		}
		if (rsb_flag(res, RSB_VALNOTVALID))
			seq_printf(s, " (INVALID)");
		seq_printf(s, "\n");
	}

	root_list = !list_empty(&res->res_root_list);
	recover_list = !list_empty(&res->res_recover_list);

	if (root_list || recover_list) {
		seq_printf(s, "Recovery: root %d recover %d flags %lx "
			   "count %d\n", root_list, recover_list,
			   res->res_flags, res->res_recover_locks_count);
	}

	/* Print the locks attached to this resource */
	seq_printf(s, "Granted Queue\n");
	list_for_each_entry(lkb, &res->res_grantqueue, lkb_statequeue)
		print_format1_lock(s, lkb, res);

	seq_printf(s, "Conversion Queue\n");
	list_for_each_entry(lkb, &res->res_convertqueue, lkb_statequeue)
		print_format1_lock(s, lkb, res);

	seq_printf(s, "Waiting Queue\n");
	list_for_each_entry(lkb, &res->res_waitqueue, lkb_statequeue)
		print_format1_lock(s, lkb, res);

	if (list_empty(&res->res_lookup))
		goto out;

	seq_printf(s, "Lookup Queue\n");
	list_for_each_entry(lkb, &res->res_lookup, lkb_rsb_lookup) {
		seq_printf(s, "%08x %s", lkb->lkb_id,
			   print_lockmode(lkb->lkb_rqmode));
		if (lkb->lkb_wait_type)
			seq_printf(s, " wait_type: %d", lkb->lkb_wait_type);
		seq_printf(s, "\n");
	}
 out:
	unlock_rsb(res);
	return 0;
}

static void print_format2_lock(struct seq_file *s, struct dlm_lkb *lkb,
			       struct dlm_rsb *r)
{
	u64 xid = 0;
	u64 us;

	if (lkb->lkb_flags & DLM_IFL_USER) {
		if (lkb->lkb_ua)
			xid = lkb->lkb_ua->xid;
	}

	/* microseconds since lkb was added to current queue */
	us = ktime_to_us(ktime_sub(ktime_get(), lkb->lkb_timestamp));

	/* id nodeid remid pid xid exflags flags sts grmode rqmode time_us
	   r_nodeid r_len r_name */

	seq_printf(s, "%x %d %x %u %llu %x %x %d %d %d %llu %u %d \"%s\"\n",
		   lkb->lkb_id,
		   lkb->lkb_nodeid,
		   lkb->lkb_remid,
		   lkb->lkb_ownpid,
		   (unsigned long long)xid,
		   lkb->lkb_exflags,
		   lkb->lkb_flags,
		   lkb->lkb_status,
		   lkb->lkb_grmode,
		   lkb->lkb_rqmode,
		   (unsigned long long)us,
		   r->res_nodeid,
		   r->res_length,
		   r->res_name);
}

static int print_format2(struct dlm_rsb *r, struct seq_file *s)
{
	struct dlm_lkb *lkb;

	lock_rsb(r);

	list_for_each_entry(lkb, &r->res_grantqueue, lkb_statequeue)
		print_format2_lock(s, lkb, r);

	list_for_each_entry(lkb, &r->res_convertqueue, lkb_statequeue)
		print_format2_lock(s, lkb, r);

	list_for_each_entry(lkb, &r->res_waitqueue, lkb_statequeue)
		print_format2_lock(s, lkb, r);

	unlock_rsb(r);
	return 0;
}

static void print_format3_lock(struct seq_file *s, struct dlm_lkb *lkb,
			       int rsb_lookup)
{
	u64 xid = 0;

	if (lkb->lkb_flags & DLM_IFL_USER) {
		if (lkb->lkb_ua)
			xid = lkb->lkb_ua->xid;
	}

	seq_printf(s, "lkb %x %d %x %u %llu %x %x %d %d %d %d %d %d %u %llu %llu\n",
		   lkb->lkb_id,
		   lkb->lkb_nodeid,
		   lkb->lkb_remid,
		   lkb->lkb_ownpid,
		   (unsigned long long)xid,
		   lkb->lkb_exflags,
		   lkb->lkb_flags,
		   lkb->lkb_status,
		   lkb->lkb_grmode,
		   lkb->lkb_rqmode,
		   lkb->lkb_highbast,
		   rsb_lookup,
		   lkb->lkb_wait_type,
		   lkb->lkb_lvbseq,
		   (unsigned long long)ktime_to_ns(lkb->lkb_timestamp),
		   (unsigned long long)ktime_to_ns(lkb->lkb_time_bast));
}

static int print_format3(struct dlm_rsb *r, struct seq_file *s)
{
	struct dlm_lkb *lkb;
	int i, lvblen = r->res_ls->ls_lvblen;
	int print_name = 1;

	lock_rsb(r);

	seq_printf(s, "rsb %p %d %x %lx %d %d %u %d ",
		   r,
		   r->res_nodeid,
		   r->res_first_lkid,
		   r->res_flags,
		   !list_empty(&r->res_root_list),
		   !list_empty(&r->res_recover_list),
		   r->res_recover_locks_count,
		   r->res_length);

	for (i = 0; i < r->res_length; i++) {
		if (!isascii(r->res_name[i]) || !isprint(r->res_name[i]))
			print_name = 0;
	}

	seq_printf(s, "%s", print_name ? "str " : "hex");

	for (i = 0; i < r->res_length; i++) {
		if (print_name)
			seq_printf(s, "%c", r->res_name[i]);
		else
			seq_printf(s, " %02x", (unsigned char)r->res_name[i]);
	}
	seq_printf(s, "\n");

	if (!r->res_lvbptr)
		goto do_locks;

	seq_printf(s, "lvb %u %d", r->res_lvbseq, lvblen);

	for (i = 0; i < lvblen; i++)
		seq_printf(s, " %02x", (unsigned char)r->res_lvbptr[i]);
	seq_printf(s, "\n");

 do_locks:
	list_for_each_entry(lkb, &r->res_grantqueue, lkb_statequeue)
		print_format3_lock(s, lkb, 0);

	list_for_each_entry(lkb, &r->res_convertqueue, lkb_statequeue)
		print_format3_lock(s, lkb, 0);

	list_for_each_entry(lkb, &r->res_waitqueue, lkb_statequeue)
		print_format3_lock(s, lkb, 0);

	list_for_each_entry(lkb, &r->res_lookup, lkb_rsb_lookup)
		print_format3_lock(s, lkb, 1);

	unlock_rsb(r);
	return 0;
}

static int rsb_iter_next(struct rsb_iter *ri)
{
	struct dlm_ls *ls = ri->ls;
	int i;

	if (!ri->next) {
 top:
		/* Find the next non-empty hash bucket */
		for (i = ri->entry; i < ls->ls_rsbtbl_size; i++) {
			read_lock(&ls->ls_rsbtbl[i].lock);
			if (!list_empty(&ls->ls_rsbtbl[i].list)) {
				ri->next = ls->ls_rsbtbl[i].list.next;
				ri->rsb = list_entry(ri->next, struct dlm_rsb,
							res_hashchain);
				dlm_hold_rsb(ri->rsb);
				read_unlock(&ls->ls_rsbtbl[i].lock);
				break;
			}
			read_unlock(&ls->ls_rsbtbl[i].lock);
		}
		ri->entry = i;

		if (ri->entry >= ls->ls_rsbtbl_size)
			return 1;
	} else {
		struct dlm_rsb *old = ri->rsb;
		i = ri->entry;
		read_lock(&ls->ls_rsbtbl[i].lock);
		ri->next = ri->next->next;
		if (ri->next->next == ls->ls_rsbtbl[i].list.next) {
			/* End of list - move to next bucket */
			ri->next = NULL;
			ri->entry++;
			read_unlock(&ls->ls_rsbtbl[i].lock);
			dlm_put_rsb(old);
			goto top;
		}
		ri->rsb = list_entry(ri->next, struct dlm_rsb, res_hashchain);
		dlm_hold_rsb(ri->rsb);
		read_unlock(&ls->ls_rsbtbl[i].lock);
		dlm_put_rsb(old);
	}

	return 0;
}

static void rsb_iter_free(struct rsb_iter *ri)
{
	kfree(ri);
}

static struct rsb_iter *rsb_iter_init(struct dlm_ls *ls)
{
	struct rsb_iter *ri;

	ri = kzalloc(sizeof *ri, GFP_KERNEL);
	if (!ri)
		return NULL;

	ri->ls = ls;
	ri->entry = 0;
	ri->next = NULL;
	ri->format = 1;

	if (rsb_iter_next(ri)) {
		rsb_iter_free(ri);
		return NULL;
	}

	return ri;
}

static void *rsb_seq_start(struct seq_file *file, loff_t *pos)
{
	struct rsb_iter *ri;
	loff_t n = *pos;

	ri = rsb_iter_init(file->private);
	if (!ri)
		return NULL;

	while (n--) {
		if (rsb_iter_next(ri)) {
			rsb_iter_free(ri);
			return NULL;
		}
	}

	return ri;
}

static void *rsb_seq_next(struct seq_file *file, void *iter_ptr, loff_t *pos)
{
	struct rsb_iter *ri = iter_ptr;

	(*pos)++;

	if (rsb_iter_next(ri)) {
		rsb_iter_free(ri);
		return NULL;
	}

	return ri;
}

static void rsb_seq_stop(struct seq_file *file, void *iter_ptr)
{
	/* nothing for now */
}

static int rsb_seq_show(struct seq_file *file, void *iter_ptr)
{
	struct rsb_iter *ri = iter_ptr;

	switch (ri->format) {
	case 1:
		print_format1(ri->rsb, file);
		break;
	case 2:
		if (ri->header) {
			seq_printf(file, "id nodeid remid pid xid exflags "
					 "flags sts grmode rqmode time_ms "
					 "r_nodeid r_len r_name\n");
			ri->header = 0;
		}
		print_format2(ri->rsb, file);
		break;
	case 3:
		if (ri->header) {
			seq_printf(file, "version rsb 1.1 lvb 1.1 lkb 1.1\n");
			ri->header = 0;
		}
		print_format3(ri->rsb, file);
		break;
	}

	return 0;
}

static struct seq_operations rsb_seq_ops = {
	.start = rsb_seq_start,
	.next  = rsb_seq_next,
	.stop  = rsb_seq_stop,
	.show  = rsb_seq_show,
};

static int rsb_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int ret;

	ret = seq_open(file, &rsb_seq_ops);
	if (ret)
		return ret;

	seq = file->private_data;
	seq->private = inode->i_private;

	return 0;
}

static const struct file_operations rsb_fops = {
	.owner   = THIS_MODULE,
	.open    = rsb_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

/*
 * Dump state in compact per-lock listing
 */

static struct rsb_iter *locks_iter_init(struct dlm_ls *ls, loff_t *pos)
{
	struct rsb_iter *ri;

	ri = kzalloc(sizeof *ri, GFP_KERNEL);
	if (!ri)
		return NULL;

	ri->ls = ls;
	ri->entry = 0;
	ri->next = NULL;
	ri->format = 2;

	if (*pos == 0)
		ri->header = 1;

	if (rsb_iter_next(ri)) {
		rsb_iter_free(ri);
		return NULL;
	}

	return ri;
}

static void *locks_seq_start(struct seq_file *file, loff_t *pos)
{
	struct rsb_iter *ri;
	loff_t n = *pos;

	ri = locks_iter_init(file->private, pos);
	if (!ri)
		return NULL;

	while (n--) {
		if (rsb_iter_next(ri)) {
			rsb_iter_free(ri);
			return NULL;
		}
	}

	return ri;
}

static struct seq_operations locks_seq_ops = {
	.start = locks_seq_start,
	.next  = rsb_seq_next,
	.stop  = rsb_seq_stop,
	.show  = rsb_seq_show,
};

static int locks_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int ret;

	ret = seq_open(file, &locks_seq_ops);
	if (ret)
		return ret;

	seq = file->private_data;
	seq->private = inode->i_private;

	return 0;
}

static const struct file_operations locks_fops = {
	.owner   = THIS_MODULE,
	.open    = locks_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

/*
 * Dump all rsb/lvb/lkb state in compact listing, more complete than _locks
 * This can replace both formats 1 and 2 eventually.
 */

static struct rsb_iter *all_iter_init(struct dlm_ls *ls, loff_t *pos)
{
	struct rsb_iter *ri;

	ri = kzalloc(sizeof *ri, GFP_KERNEL);
	if (!ri)
		return NULL;

	ri->ls = ls;
	ri->entry = 0;
	ri->next = NULL;
	ri->format = 3;

	if (*pos == 0)
		ri->header = 1;

	if (rsb_iter_next(ri)) {
		rsb_iter_free(ri);
		return NULL;
	}

	return ri;
}

static void *all_seq_start(struct seq_file *file, loff_t *pos)
{
	struct rsb_iter *ri;
	loff_t n = *pos;

	ri = all_iter_init(file->private, pos);
	if (!ri)
		return NULL;

	while (n--) {
		if (rsb_iter_next(ri)) {
			rsb_iter_free(ri);
			return NULL;
		}
	}

	return ri;
}

static struct seq_operations all_seq_ops = {
	.start = all_seq_start,
	.next  = rsb_seq_next,
	.stop  = rsb_seq_stop,
	.show  = rsb_seq_show,
};

static int all_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int ret;

	ret = seq_open(file, &all_seq_ops);
	if (ret)
		return ret;

	seq = file->private_data;
	seq->private = inode->i_private;

	return 0;
}

static const struct file_operations all_fops = {
	.owner   = THIS_MODULE,
	.open    = all_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

/*
 * dump lkb's on the ls_waiters list
 */

static int waiters_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t waiters_read(struct file *file, char __user *userbuf,
			    size_t count, loff_t *ppos)
{
	struct dlm_ls *ls = file->private_data;
	struct dlm_lkb *lkb;
	size_t len = DLM_DEBUG_BUF_LEN, pos = 0, ret, rv;

	mutex_lock(&debug_buf_lock);
	mutex_lock(&ls->ls_waiters_mutex);
	memset(debug_buf, 0, sizeof(debug_buf));

	list_for_each_entry(lkb, &ls->ls_waiters, lkb_wait_reply) {
		ret = snprintf(debug_buf + pos, len - pos, "%x %d %d %s\n",
			       lkb->lkb_id, lkb->lkb_wait_type,
			       lkb->lkb_nodeid, lkb->lkb_resource->res_name);
		if (ret >= len - pos)
			break;
		pos += ret;
	}
	mutex_unlock(&ls->ls_waiters_mutex);

	rv = simple_read_from_buffer(userbuf, count, ppos, debug_buf, pos);
	mutex_unlock(&debug_buf_lock);
	return rv;
}

static const struct file_operations waiters_fops = {
	.owner   = THIS_MODULE,
	.open    = waiters_open,
	.read    = waiters_read
};

void dlm_delete_debug_file(struct dlm_ls *ls)
{
	if (ls->ls_debug_rsb_dentry)
		debugfs_remove(ls->ls_debug_rsb_dentry);
	if (ls->ls_debug_waiters_dentry)
		debugfs_remove(ls->ls_debug_waiters_dentry);
	if (ls->ls_debug_locks_dentry)
		debugfs_remove(ls->ls_debug_locks_dentry);
	if (ls->ls_debug_all_dentry)
		debugfs_remove(ls->ls_debug_all_dentry);
}

int dlm_create_debug_file(struct dlm_ls *ls)
{
	char name[DLM_LOCKSPACE_LEN+8];

	/* format 1 */

	ls->ls_debug_rsb_dentry = debugfs_create_file(ls->ls_name,
						      S_IFREG | S_IRUGO,
						      dlm_root,
						      ls,
						      &rsb_fops);
	if (!ls->ls_debug_rsb_dentry)
		goto fail;

	/* format 2 */

	memset(name, 0, sizeof(name));
	snprintf(name, DLM_LOCKSPACE_LEN+8, "%s_locks", ls->ls_name);

	ls->ls_debug_locks_dentry = debugfs_create_file(name,
							S_IFREG | S_IRUGO,
							dlm_root,
							ls,
							&locks_fops);
	if (!ls->ls_debug_locks_dentry)
		goto fail;

	/* format 3 */

	memset(name, 0, sizeof(name));
	snprintf(name, DLM_LOCKSPACE_LEN+8, "%s_all", ls->ls_name);

	ls->ls_debug_all_dentry = debugfs_create_file(name,
						      S_IFREG | S_IRUGO,
						      dlm_root,
						      ls,
						      &all_fops);
	if (!ls->ls_debug_all_dentry)
		goto fail;

	memset(name, 0, sizeof(name));
	snprintf(name, DLM_LOCKSPACE_LEN+8, "%s_waiters", ls->ls_name);

	ls->ls_debug_waiters_dentry = debugfs_create_file(name,
							  S_IFREG | S_IRUGO,
							  dlm_root,
							  ls,
							  &waiters_fops);
	if (!ls->ls_debug_waiters_dentry)
		goto fail;

	return 0;

 fail:
	dlm_delete_debug_file(ls);
	return -ENOMEM;
}

int __init dlm_register_debugfs(void)
{
	mutex_init(&debug_buf_lock);
	dlm_root = debugfs_create_dir("dlm", NULL);
	return dlm_root ? 0 : -ENOMEM;
}

void dlm_unregister_debugfs(void)
{
	debugfs_remove(dlm_root);
}

