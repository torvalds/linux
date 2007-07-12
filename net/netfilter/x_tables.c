/*
 * x_tables core - Backend for {ip,ip6,arp}_tables
 *
 * Copyright (C) 2006-2006 Harald Welte <laforge@netfilter.org>
 *
 * Based on existing ip_tables code which is
 *   Copyright (C) 1999 Paul `Rusty' Russell & Michael J. Neuling
 *   Copyright (C) 2000-2005 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/mm.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_arp.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("[ip,ip6,arp]_tables backend module");

#define SMP_ALIGN(x) (((x) + SMP_CACHE_BYTES-1) & ~(SMP_CACHE_BYTES-1))

struct xt_af {
	struct mutex mutex;
	struct list_head match;
	struct list_head target;
	struct list_head tables;
	struct mutex compat_mutex;
};

static struct xt_af *xt;

#ifdef DEBUG_IP_FIREWALL_USER
#define duprintf(format, args...) printk(format , ## args)
#else
#define duprintf(format, args...)
#endif

enum {
	TABLE,
	TARGET,
	MATCH,
};

static const char *xt_prefix[NPROTO] = {
	[AF_INET]	= "ip",
	[AF_INET6]	= "ip6",
	[NF_ARP]	= "arp",
};

/* Registration hooks for targets. */
int
xt_register_target(struct xt_target *target)
{
	int ret, af = target->family;

	ret = mutex_lock_interruptible(&xt[af].mutex);
	if (ret != 0)
		return ret;
	list_add(&target->list, &xt[af].target);
	mutex_unlock(&xt[af].mutex);
	return ret;
}
EXPORT_SYMBOL(xt_register_target);

void
xt_unregister_target(struct xt_target *target)
{
	int af = target->family;

	mutex_lock(&xt[af].mutex);
	list_del(&target->list);
	mutex_unlock(&xt[af].mutex);
}
EXPORT_SYMBOL(xt_unregister_target);

int
xt_register_targets(struct xt_target *target, unsigned int n)
{
	unsigned int i;
	int err = 0;

	for (i = 0; i < n; i++) {
		err = xt_register_target(&target[i]);
		if (err)
			goto err;
	}
	return err;

err:
	if (i > 0)
		xt_unregister_targets(target, i);
	return err;
}
EXPORT_SYMBOL(xt_register_targets);

void
xt_unregister_targets(struct xt_target *target, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; i++)
		xt_unregister_target(&target[i]);
}
EXPORT_SYMBOL(xt_unregister_targets);

int
xt_register_match(struct xt_match *match)
{
	int ret, af = match->family;

	ret = mutex_lock_interruptible(&xt[af].mutex);
	if (ret != 0)
		return ret;

	list_add(&match->list, &xt[af].match);
	mutex_unlock(&xt[af].mutex);

	return ret;
}
EXPORT_SYMBOL(xt_register_match);

void
xt_unregister_match(struct xt_match *match)
{
	int af =  match->family;

	mutex_lock(&xt[af].mutex);
	list_del(&match->list);
	mutex_unlock(&xt[af].mutex);
}
EXPORT_SYMBOL(xt_unregister_match);

int
xt_register_matches(struct xt_match *match, unsigned int n)
{
	unsigned int i;
	int err = 0;

	for (i = 0; i < n; i++) {
		err = xt_register_match(&match[i]);
		if (err)
			goto err;
	}
	return err;

err:
	if (i > 0)
		xt_unregister_matches(match, i);
	return err;
}
EXPORT_SYMBOL(xt_register_matches);

void
xt_unregister_matches(struct xt_match *match, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; i++)
		xt_unregister_match(&match[i]);
}
EXPORT_SYMBOL(xt_unregister_matches);


/*
 * These are weird, but module loading must not be done with mutex
 * held (since they will register), and we have to have a single
 * function to use try_then_request_module().
 */

/* Find match, grabs ref.  Returns ERR_PTR() on error. */
struct xt_match *xt_find_match(int af, const char *name, u8 revision)
{
	struct xt_match *m;
	int err = 0;

	if (mutex_lock_interruptible(&xt[af].mutex) != 0)
		return ERR_PTR(-EINTR);

	list_for_each_entry(m, &xt[af].match, list) {
		if (strcmp(m->name, name) == 0) {
			if (m->revision == revision) {
				if (try_module_get(m->me)) {
					mutex_unlock(&xt[af].mutex);
					return m;
				}
			} else
				err = -EPROTOTYPE; /* Found something. */
		}
	}
	mutex_unlock(&xt[af].mutex);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(xt_find_match);

/* Find target, grabs ref.  Returns ERR_PTR() on error. */
struct xt_target *xt_find_target(int af, const char *name, u8 revision)
{
	struct xt_target *t;
	int err = 0;

	if (mutex_lock_interruptible(&xt[af].mutex) != 0)
		return ERR_PTR(-EINTR);

	list_for_each_entry(t, &xt[af].target, list) {
		if (strcmp(t->name, name) == 0) {
			if (t->revision == revision) {
				if (try_module_get(t->me)) {
					mutex_unlock(&xt[af].mutex);
					return t;
				}
			} else
				err = -EPROTOTYPE; /* Found something. */
		}
	}
	mutex_unlock(&xt[af].mutex);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(xt_find_target);

struct xt_target *xt_request_find_target(int af, const char *name, u8 revision)
{
	struct xt_target *target;

	target = try_then_request_module(xt_find_target(af, name, revision),
					 "%st_%s", xt_prefix[af], name);
	if (IS_ERR(target) || !target)
		return NULL;
	return target;
}
EXPORT_SYMBOL_GPL(xt_request_find_target);

static int match_revfn(int af, const char *name, u8 revision, int *bestp)
{
	struct xt_match *m;
	int have_rev = 0;

	list_for_each_entry(m, &xt[af].match, list) {
		if (strcmp(m->name, name) == 0) {
			if (m->revision > *bestp)
				*bestp = m->revision;
			if (m->revision == revision)
				have_rev = 1;
		}
	}
	return have_rev;
}

static int target_revfn(int af, const char *name, u8 revision, int *bestp)
{
	struct xt_target *t;
	int have_rev = 0;

	list_for_each_entry(t, &xt[af].target, list) {
		if (strcmp(t->name, name) == 0) {
			if (t->revision > *bestp)
				*bestp = t->revision;
			if (t->revision == revision)
				have_rev = 1;
		}
	}
	return have_rev;
}

/* Returns true or false (if no such extension at all) */
int xt_find_revision(int af, const char *name, u8 revision, int target,
		     int *err)
{
	int have_rev, best = -1;

	if (mutex_lock_interruptible(&xt[af].mutex) != 0) {
		*err = -EINTR;
		return 1;
	}
	if (target == 1)
		have_rev = target_revfn(af, name, revision, &best);
	else
		have_rev = match_revfn(af, name, revision, &best);
	mutex_unlock(&xt[af].mutex);

	/* Nothing at all?  Return 0 to try loading module. */
	if (best == -1) {
		*err = -ENOENT;
		return 0;
	}

	*err = best;
	if (!have_rev)
		*err = -EPROTONOSUPPORT;
	return 1;
}
EXPORT_SYMBOL_GPL(xt_find_revision);

int xt_check_match(const struct xt_match *match, unsigned short family,
		   unsigned int size, const char *table, unsigned int hook_mask,
		   unsigned short proto, int inv_proto)
{
	if (XT_ALIGN(match->matchsize) != size) {
		printk("%s_tables: %s match: invalid size %Zu != %u\n",
		       xt_prefix[family], match->name,
		       XT_ALIGN(match->matchsize), size);
		return -EINVAL;
	}
	if (match->table && strcmp(match->table, table)) {
		printk("%s_tables: %s match: only valid in %s table, not %s\n",
		       xt_prefix[family], match->name, match->table, table);
		return -EINVAL;
	}
	if (match->hooks && (hook_mask & ~match->hooks) != 0) {
		printk("%s_tables: %s match: bad hook_mask %u/%u\n",
		       xt_prefix[family], match->name, hook_mask, match->hooks);
		return -EINVAL;
	}
	if (match->proto && (match->proto != proto || inv_proto)) {
		printk("%s_tables: %s match: only valid for protocol %u\n",
		       xt_prefix[family], match->name, match->proto);
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(xt_check_match);

#ifdef CONFIG_COMPAT
int xt_compat_match_offset(struct xt_match *match)
{
	u_int16_t csize = match->compatsize ? : match->matchsize;
	return XT_ALIGN(match->matchsize) - COMPAT_XT_ALIGN(csize);
}
EXPORT_SYMBOL_GPL(xt_compat_match_offset);

void xt_compat_match_from_user(struct xt_entry_match *m, void **dstptr,
			       int *size)
{
	struct xt_match *match = m->u.kernel.match;
	struct compat_xt_entry_match *cm = (struct compat_xt_entry_match *)m;
	int pad, off = xt_compat_match_offset(match);
	u_int16_t msize = cm->u.user.match_size;

	m = *dstptr;
	memcpy(m, cm, sizeof(*cm));
	if (match->compat_from_user)
		match->compat_from_user(m->data, cm->data);
	else
		memcpy(m->data, cm->data, msize - sizeof(*cm));
	pad = XT_ALIGN(match->matchsize) - match->matchsize;
	if (pad > 0)
		memset(m->data + match->matchsize, 0, pad);

	msize += off;
	m->u.user.match_size = msize;

	*size += off;
	*dstptr += msize;
}
EXPORT_SYMBOL_GPL(xt_compat_match_from_user);

int xt_compat_match_to_user(struct xt_entry_match *m, void __user **dstptr,
			    int *size)
{
	struct xt_match *match = m->u.kernel.match;
	struct compat_xt_entry_match __user *cm = *dstptr;
	int off = xt_compat_match_offset(match);
	u_int16_t msize = m->u.user.match_size - off;

	if (copy_to_user(cm, m, sizeof(*cm)) ||
	    put_user(msize, &cm->u.user.match_size))
		return -EFAULT;

	if (match->compat_to_user) {
		if (match->compat_to_user((void __user *)cm->data, m->data))
			return -EFAULT;
	} else {
		if (copy_to_user(cm->data, m->data, msize - sizeof(*cm)))
			return -EFAULT;
	}

	*size -= off;
	*dstptr += msize;
	return 0;
}
EXPORT_SYMBOL_GPL(xt_compat_match_to_user);
#endif /* CONFIG_COMPAT */

int xt_check_target(const struct xt_target *target, unsigned short family,
		    unsigned int size, const char *table, unsigned int hook_mask,
		    unsigned short proto, int inv_proto)
{
	if (XT_ALIGN(target->targetsize) != size) {
		printk("%s_tables: %s target: invalid size %Zu != %u\n",
		       xt_prefix[family], target->name,
		       XT_ALIGN(target->targetsize), size);
		return -EINVAL;
	}
	if (target->table && strcmp(target->table, table)) {
		printk("%s_tables: %s target: only valid in %s table, not %s\n",
		       xt_prefix[family], target->name, target->table, table);
		return -EINVAL;
	}
	if (target->hooks && (hook_mask & ~target->hooks) != 0) {
		printk("%s_tables: %s target: bad hook_mask %u/%u\n",
		       xt_prefix[family], target->name, hook_mask,
		       target->hooks);
		return -EINVAL;
	}
	if (target->proto && (target->proto != proto || inv_proto)) {
		printk("%s_tables: %s target: only valid for protocol %u\n",
		       xt_prefix[family], target->name, target->proto);
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(xt_check_target);

#ifdef CONFIG_COMPAT
int xt_compat_target_offset(struct xt_target *target)
{
	u_int16_t csize = target->compatsize ? : target->targetsize;
	return XT_ALIGN(target->targetsize) - COMPAT_XT_ALIGN(csize);
}
EXPORT_SYMBOL_GPL(xt_compat_target_offset);

void xt_compat_target_from_user(struct xt_entry_target *t, void **dstptr,
				int *size)
{
	struct xt_target *target = t->u.kernel.target;
	struct compat_xt_entry_target *ct = (struct compat_xt_entry_target *)t;
	int pad, off = xt_compat_target_offset(target);
	u_int16_t tsize = ct->u.user.target_size;

	t = *dstptr;
	memcpy(t, ct, sizeof(*ct));
	if (target->compat_from_user)
		target->compat_from_user(t->data, ct->data);
	else
		memcpy(t->data, ct->data, tsize - sizeof(*ct));
	pad = XT_ALIGN(target->targetsize) - target->targetsize;
	if (pad > 0)
		memset(t->data + target->targetsize, 0, pad);

	tsize += off;
	t->u.user.target_size = tsize;

	*size += off;
	*dstptr += tsize;
}
EXPORT_SYMBOL_GPL(xt_compat_target_from_user);

int xt_compat_target_to_user(struct xt_entry_target *t, void __user **dstptr,
			     int *size)
{
	struct xt_target *target = t->u.kernel.target;
	struct compat_xt_entry_target __user *ct = *dstptr;
	int off = xt_compat_target_offset(target);
	u_int16_t tsize = t->u.user.target_size - off;

	if (copy_to_user(ct, t, sizeof(*ct)) ||
	    put_user(tsize, &ct->u.user.target_size))
		return -EFAULT;

	if (target->compat_to_user) {
		if (target->compat_to_user((void __user *)ct->data, t->data))
			return -EFAULT;
	} else {
		if (copy_to_user(ct->data, t->data, tsize - sizeof(*ct)))
			return -EFAULT;
	}

	*size -= off;
	*dstptr += tsize;
	return 0;
}
EXPORT_SYMBOL_GPL(xt_compat_target_to_user);
#endif

struct xt_table_info *xt_alloc_table_info(unsigned int size)
{
	struct xt_table_info *newinfo;
	int cpu;

	/* Pedantry: prevent them from hitting BUG() in vmalloc.c --RR */
	if ((SMP_ALIGN(size) >> PAGE_SHIFT) + 2 > num_physpages)
		return NULL;

	newinfo = kzalloc(sizeof(struct xt_table_info), GFP_KERNEL);
	if (!newinfo)
		return NULL;

	newinfo->size = size;

	for_each_possible_cpu(cpu) {
		if (size <= PAGE_SIZE)
			newinfo->entries[cpu] = kmalloc_node(size,
							GFP_KERNEL,
							cpu_to_node(cpu));
		else
			newinfo->entries[cpu] = vmalloc_node(size,
							cpu_to_node(cpu));

		if (newinfo->entries[cpu] == NULL) {
			xt_free_table_info(newinfo);
			return NULL;
		}
	}

	return newinfo;
}
EXPORT_SYMBOL(xt_alloc_table_info);

void xt_free_table_info(struct xt_table_info *info)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		if (info->size <= PAGE_SIZE)
			kfree(info->entries[cpu]);
		else
			vfree(info->entries[cpu]);
	}
	kfree(info);
}
EXPORT_SYMBOL(xt_free_table_info);

/* Find table by name, grabs mutex & ref.  Returns ERR_PTR() on error. */
struct xt_table *xt_find_table_lock(int af, const char *name)
{
	struct xt_table *t;

	if (mutex_lock_interruptible(&xt[af].mutex) != 0)
		return ERR_PTR(-EINTR);

	list_for_each_entry(t, &xt[af].tables, list)
		if (strcmp(t->name, name) == 0 && try_module_get(t->me))
			return t;
	mutex_unlock(&xt[af].mutex);
	return NULL;
}
EXPORT_SYMBOL_GPL(xt_find_table_lock);

void xt_table_unlock(struct xt_table *table)
{
	mutex_unlock(&xt[table->af].mutex);
}
EXPORT_SYMBOL_GPL(xt_table_unlock);

#ifdef CONFIG_COMPAT
void xt_compat_lock(int af)
{
	mutex_lock(&xt[af].compat_mutex);
}
EXPORT_SYMBOL_GPL(xt_compat_lock);

void xt_compat_unlock(int af)
{
	mutex_unlock(&xt[af].compat_mutex);
}
EXPORT_SYMBOL_GPL(xt_compat_unlock);
#endif

struct xt_table_info *
xt_replace_table(struct xt_table *table,
	      unsigned int num_counters,
	      struct xt_table_info *newinfo,
	      int *error)
{
	struct xt_table_info *oldinfo, *private;

	/* Do the substitution. */
	write_lock_bh(&table->lock);
	private = table->private;
	/* Check inside lock: is the old number correct? */
	if (num_counters != private->number) {
		duprintf("num_counters != table->private->number (%u/%u)\n",
			 num_counters, private->number);
		write_unlock_bh(&table->lock);
		*error = -EAGAIN;
		return NULL;
	}
	oldinfo = private;
	table->private = newinfo;
	newinfo->initial_entries = oldinfo->initial_entries;
	write_unlock_bh(&table->lock);

	return oldinfo;
}
EXPORT_SYMBOL_GPL(xt_replace_table);

int xt_register_table(struct xt_table *table,
		      struct xt_table_info *bootstrap,
		      struct xt_table_info *newinfo)
{
	int ret;
	struct xt_table_info *private;
	struct xt_table *t;

	ret = mutex_lock_interruptible(&xt[table->af].mutex);
	if (ret != 0)
		return ret;

	/* Don't autoload: we'd eat our tail... */
	list_for_each_entry(t, &xt[table->af].tables, list) {
		if (strcmp(t->name, table->name) == 0) {
			ret = -EEXIST;
			goto unlock;
		}
	}

	/* Simplifies replace_table code. */
	table->private = bootstrap;
	rwlock_init(&table->lock);
	if (!xt_replace_table(table, 0, newinfo, &ret))
		goto unlock;

	private = table->private;
	duprintf("table->private->number = %u\n", private->number);

	/* save number of initial entries */
	private->initial_entries = private->number;

	list_add(&table->list, &xt[table->af].tables);

	ret = 0;
 unlock:
	mutex_unlock(&xt[table->af].mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(xt_register_table);

void *xt_unregister_table(struct xt_table *table)
{
	struct xt_table_info *private;

	mutex_lock(&xt[table->af].mutex);
	private = table->private;
	list_del(&table->list);
	mutex_unlock(&xt[table->af].mutex);

	return private;
}
EXPORT_SYMBOL_GPL(xt_unregister_table);

#ifdef CONFIG_PROC_FS
static struct list_head *xt_get_idx(struct list_head *list, struct seq_file *seq, loff_t pos)
{
	struct list_head *head = list->next;

	if (!head || list_empty(list))
		return NULL;

	while (pos && (head = head->next)) {
		if (head == list)
			return NULL;
		pos--;
	}
	return pos ? NULL : head;
}

static struct list_head *type2list(u_int16_t af, u_int16_t type)
{
	struct list_head *list;

	switch (type) {
	case TARGET:
		list = &xt[af].target;
		break;
	case MATCH:
		list = &xt[af].match;
		break;
	case TABLE:
		list = &xt[af].tables;
		break;
	default:
		list = NULL;
		break;
	}

	return list;
}

static void *xt_tgt_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct proc_dir_entry *pde = (struct proc_dir_entry *) seq->private;
	u_int16_t af = (unsigned long)pde->data & 0xffff;
	u_int16_t type = (unsigned long)pde->data >> 16;
	struct list_head *list;

	if (af >= NPROTO)
		return NULL;

	list = type2list(af, type);
	if (!list)
		return NULL;

	if (mutex_lock_interruptible(&xt[af].mutex) != 0)
		return NULL;

	return xt_get_idx(list, seq, *pos);
}

static void *xt_tgt_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct proc_dir_entry *pde = seq->private;
	u_int16_t af = (unsigned long)pde->data & 0xffff;
	u_int16_t type = (unsigned long)pde->data >> 16;
	struct list_head *list;

	if (af >= NPROTO)
		return NULL;

	list = type2list(af, type);
	if (!list)
		return NULL;

	(*pos)++;
	return xt_get_idx(list, seq, *pos);
}

static void xt_tgt_seq_stop(struct seq_file *seq, void *v)
{
	struct proc_dir_entry *pde = seq->private;
	u_int16_t af = (unsigned long)pde->data & 0xffff;

	mutex_unlock(&xt[af].mutex);
}

static int xt_name_seq_show(struct seq_file *seq, void *v)
{
	char *name = (char *)v + sizeof(struct list_head);

	if (strlen(name))
		return seq_printf(seq, "%s\n", name);
	else
		return 0;
}

static const struct seq_operations xt_tgt_seq_ops = {
	.start	= xt_tgt_seq_start,
	.next	= xt_tgt_seq_next,
	.stop	= xt_tgt_seq_stop,
	.show	= xt_name_seq_show,
};

static int xt_tgt_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = seq_open(file, &xt_tgt_seq_ops);
	if (!ret) {
		struct seq_file *seq = file->private_data;
		struct proc_dir_entry *pde = PDE(inode);

		seq->private = pde;
	}

	return ret;
}

static const struct file_operations xt_file_ops = {
	.owner	 = THIS_MODULE,
	.open	 = xt_tgt_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

#define FORMAT_TABLES	"_tables_names"
#define	FORMAT_MATCHES	"_tables_matches"
#define FORMAT_TARGETS 	"_tables_targets"

#endif /* CONFIG_PROC_FS */

int xt_proto_init(int af)
{
#ifdef CONFIG_PROC_FS
	char buf[XT_FUNCTION_MAXNAMELEN];
	struct proc_dir_entry *proc;
#endif

	if (af >= NPROTO)
		return -EINVAL;


#ifdef CONFIG_PROC_FS
	strlcpy(buf, xt_prefix[af], sizeof(buf));
	strlcat(buf, FORMAT_TABLES, sizeof(buf));
	proc = proc_net_fops_create(buf, 0440, &xt_file_ops);
	if (!proc)
		goto out;
	proc->data = (void *) ((unsigned long) af | (TABLE << 16));


	strlcpy(buf, xt_prefix[af], sizeof(buf));
	strlcat(buf, FORMAT_MATCHES, sizeof(buf));
	proc = proc_net_fops_create(buf, 0440, &xt_file_ops);
	if (!proc)
		goto out_remove_tables;
	proc->data = (void *) ((unsigned long) af | (MATCH << 16));

	strlcpy(buf, xt_prefix[af], sizeof(buf));
	strlcat(buf, FORMAT_TARGETS, sizeof(buf));
	proc = proc_net_fops_create(buf, 0440, &xt_file_ops);
	if (!proc)
		goto out_remove_matches;
	proc->data = (void *) ((unsigned long) af | (TARGET << 16));
#endif

	return 0;

#ifdef CONFIG_PROC_FS
out_remove_matches:
	strlcpy(buf, xt_prefix[af], sizeof(buf));
	strlcat(buf, FORMAT_MATCHES, sizeof(buf));
	proc_net_remove(buf);

out_remove_tables:
	strlcpy(buf, xt_prefix[af], sizeof(buf));
	strlcat(buf, FORMAT_TABLES, sizeof(buf));
	proc_net_remove(buf);
out:
	return -1;
#endif
}
EXPORT_SYMBOL_GPL(xt_proto_init);

void xt_proto_fini(int af)
{
#ifdef CONFIG_PROC_FS
	char buf[XT_FUNCTION_MAXNAMELEN];

	strlcpy(buf, xt_prefix[af], sizeof(buf));
	strlcat(buf, FORMAT_TABLES, sizeof(buf));
	proc_net_remove(buf);

	strlcpy(buf, xt_prefix[af], sizeof(buf));
	strlcat(buf, FORMAT_TARGETS, sizeof(buf));
	proc_net_remove(buf);

	strlcpy(buf, xt_prefix[af], sizeof(buf));
	strlcat(buf, FORMAT_MATCHES, sizeof(buf));
	proc_net_remove(buf);
#endif /*CONFIG_PROC_FS*/
}
EXPORT_SYMBOL_GPL(xt_proto_fini);


static int __init xt_init(void)
{
	int i;

	xt = kmalloc(sizeof(struct xt_af) * NPROTO, GFP_KERNEL);
	if (!xt)
		return -ENOMEM;

	for (i = 0; i < NPROTO; i++) {
		mutex_init(&xt[i].mutex);
#ifdef CONFIG_COMPAT
		mutex_init(&xt[i].compat_mutex);
#endif
		INIT_LIST_HEAD(&xt[i].target);
		INIT_LIST_HEAD(&xt[i].match);
		INIT_LIST_HEAD(&xt[i].tables);
	}
	return 0;
}

static void __exit xt_fini(void)
{
	kfree(xt);
}

module_init(xt_init);
module_exit(xt_fini);

