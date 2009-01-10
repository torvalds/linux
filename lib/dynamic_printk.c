/*
 * lib/dynamic_printk.c
 *
 * make pr_debug()/dev_dbg() calls runtime configurable based upon their
 * their source module.
 *
 * Copyright (C) 2008 Red Hat, Inc., Jason Baron <jbaron@redhat.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/fs.h>

extern struct mod_debug __start___verbose[];
extern struct mod_debug __stop___verbose[];

struct debug_name {
	struct hlist_node hlist;
	struct hlist_node hlist2;
	int hash1;
	int hash2;
	char *name;
	int enable;
	int type;
};

static int nr_entries;
static int num_enabled;
int dynamic_enabled = DYNAMIC_ENABLED_NONE;
static struct hlist_head module_table[DEBUG_HASH_TABLE_SIZE] =
	{ [0 ... DEBUG_HASH_TABLE_SIZE-1] = HLIST_HEAD_INIT };
static struct hlist_head module_table2[DEBUG_HASH_TABLE_SIZE] =
	{ [0 ... DEBUG_HASH_TABLE_SIZE-1] = HLIST_HEAD_INIT };
static DECLARE_MUTEX(debug_list_mutex);

/* dynamic_printk_enabled, and dynamic_printk_enabled2 are bitmasks in which
 * bit n is set to 1 if any modname hashes into the bucket n, 0 otherwise. They
 * use independent hash functions, to reduce the chance of false positives.
 */
long long dynamic_printk_enabled;
EXPORT_SYMBOL_GPL(dynamic_printk_enabled);
long long dynamic_printk_enabled2;
EXPORT_SYMBOL_GPL(dynamic_printk_enabled2);

/* returns the debug module pointer. */
static struct debug_name *find_debug_module(char *module_name)
{
	int i;
	struct hlist_head *head;
	struct hlist_node *node;
	struct debug_name *element;

	element = NULL;
	for (i = 0; i < DEBUG_HASH_TABLE_SIZE; i++) {
		head = &module_table[i];
		hlist_for_each_entry_rcu(element, node, head, hlist)
			if (!strcmp(element->name, module_name))
				return element;
	}
	return NULL;
}

/* returns the debug module pointer. */
static struct debug_name *find_debug_module_hash(char *module_name, int hash)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct debug_name *element;

	element = NULL;
	head = &module_table[hash];
	hlist_for_each_entry_rcu(element, node, head, hlist)
		if (!strcmp(element->name, module_name))
			return element;
	return NULL;
}

/* caller must hold mutex*/
static int __add_debug_module(char *mod_name, int hash, int hash2)
{
	struct debug_name *new;
	char *module_name;
	int ret = 0;

	if (find_debug_module(mod_name)) {
		ret = -EINVAL;
		goto out;
	}
	module_name = kmalloc(strlen(mod_name) + 1, GFP_KERNEL);
	if (!module_name) {
		ret = -ENOMEM;
		goto out;
	}
	module_name = strcpy(module_name, mod_name);
	module_name[strlen(mod_name)] = '\0';
	new = kzalloc(sizeof(struct debug_name), GFP_KERNEL);
	if (!new) {
		kfree(module_name);
		ret = -ENOMEM;
		goto out;
	}
	INIT_HLIST_NODE(&new->hlist);
	INIT_HLIST_NODE(&new->hlist2);
	new->name = module_name;
	new->hash1 = hash;
	new->hash2 = hash2;
	hlist_add_head_rcu(&new->hlist, &module_table[hash]);
	hlist_add_head_rcu(&new->hlist2, &module_table2[hash2]);
	nr_entries++;
out:
	return ret;
}

int unregister_dynamic_debug_module(char *mod_name)
{
	struct debug_name *element;
	int ret = 0;

	down(&debug_list_mutex);
	element = find_debug_module(mod_name);
	if (!element) {
		ret = -EINVAL;
		goto out;
	}
	hlist_del_rcu(&element->hlist);
	hlist_del_rcu(&element->hlist2);
	synchronize_rcu();
	kfree(element->name);
	if (element->enable)
		num_enabled--;
	kfree(element);
	nr_entries--;
out:
	up(&debug_list_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(unregister_dynamic_debug_module);

int register_dynamic_debug_module(char *mod_name, int type, char *share_name,
					char *flags, int hash, int hash2)
{
	struct debug_name *elem;
	int ret = 0;

	down(&debug_list_mutex);
	elem = find_debug_module(mod_name);
	if (!elem) {
		if (__add_debug_module(mod_name, hash, hash2))
			goto out;
		elem = find_debug_module(mod_name);
		if (dynamic_enabled == DYNAMIC_ENABLED_ALL &&
				!strcmp(mod_name, share_name)) {
			elem->enable = true;
			num_enabled++;
		}
	}
	elem->type |= type;
out:
	up(&debug_list_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(register_dynamic_debug_module);

int __dynamic_dbg_enabled_helper(char *mod_name, int type, int value, int hash)
{
	struct debug_name *elem;
	int ret = 0;

	if (dynamic_enabled == DYNAMIC_ENABLED_ALL)
		return 1;
	rcu_read_lock();
	elem = find_debug_module_hash(mod_name, hash);
	if (elem && elem->enable)
		ret = 1;
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(__dynamic_dbg_enabled_helper);

static void set_all(bool enable)
{
	struct debug_name *e;
	struct hlist_node *node;
	int i;
	long long enable_mask;

	for (i = 0; i < DEBUG_HASH_TABLE_SIZE; i++) {
		if (module_table[i].first != NULL) {
			hlist_for_each_entry(e, node, &module_table[i], hlist) {
				e->enable = enable;
			}
		}
	}
	if (enable)
		enable_mask = ULLONG_MAX;
	else
		enable_mask = 0;
	dynamic_printk_enabled = enable_mask;
	dynamic_printk_enabled2 = enable_mask;
}

static int disabled_hash(int i, bool first_table)
{
	struct debug_name *e;
	struct hlist_node *node;

	if (first_table) {
		hlist_for_each_entry(e, node, &module_table[i], hlist) {
			if (e->enable)
				return 0;
		}
	} else {
		hlist_for_each_entry(e, node, &module_table2[i], hlist2) {
			if (e->enable)
				return 0;
		}
	}
	return 1;
}

static ssize_t pr_debug_write(struct file *file, const char __user *buf,
				size_t length, loff_t *ppos)
{
	char *buffer, *s, *value_str, *setting_str;
	int err, value;
	struct debug_name *elem = NULL;
	int all = 0;

	if (length > PAGE_SIZE || length < 0)
		return -EINVAL;

	buffer = (char *)__get_free_page(GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	err = -EFAULT;
	if (copy_from_user(buffer, buf, length))
		goto out;

	err = -EINVAL;
	if (length < PAGE_SIZE)
		buffer[length] = '\0';
	else if (buffer[PAGE_SIZE-1])
		goto out;

	err = -EINVAL;
	down(&debug_list_mutex);

	if (strncmp("set", buffer, 3))
		goto out_up;
	s = buffer + 3;
	setting_str = strsep(&s, "=");
	if (s == NULL)
		goto out_up;
	setting_str = strstrip(setting_str);
	value_str = strsep(&s, " ");
	if (s == NULL)
		goto out_up;
	s = strstrip(s);
	if (!strncmp(s, "all", 3))
		all = 1;
	else
		elem = find_debug_module(s);
	if (!strncmp(setting_str, "enable", 6)) {
		value = !!simple_strtol(value_str, NULL, 10);
		if (all) {
			if (value) {
				set_all(true);
				num_enabled = nr_entries;
				dynamic_enabled = DYNAMIC_ENABLED_ALL;
			} else {
				set_all(false);
				num_enabled = 0;
				dynamic_enabled = DYNAMIC_ENABLED_NONE;
			}
			err = 0;
		} else if (elem) {
			if (value && (elem->enable == 0)) {
				dynamic_printk_enabled |= (1LL << elem->hash1);
				dynamic_printk_enabled2 |= (1LL << elem->hash2);
				elem->enable = 1;
				num_enabled++;
				dynamic_enabled = DYNAMIC_ENABLED_SOME;
				err = 0;
				printk(KERN_DEBUG
					"debugging enabled for module %s\n",
					elem->name);
			} else if (!value && (elem->enable == 1)) {
				elem->enable = 0;
				num_enabled--;
				if (disabled_hash(elem->hash1, true))
					dynamic_printk_enabled &=
							~(1LL << elem->hash1);
				if (disabled_hash(elem->hash2, false))
					dynamic_printk_enabled2 &=
							~(1LL << elem->hash2);
				if (num_enabled)
					dynamic_enabled = DYNAMIC_ENABLED_SOME;
				else
					dynamic_enabled = DYNAMIC_ENABLED_NONE;
				err = 0;
				printk(KERN_DEBUG
					"debugging disabled for module %s\n",
					elem->name);
			}
		}
	}
	if (!err)
		err = length;
out_up:
	up(&debug_list_mutex);
out:
	free_page((unsigned long)buffer);
	return err;
}

static void *pr_debug_seq_start(struct seq_file *f, loff_t *pos)
{
	return (*pos < DEBUG_HASH_TABLE_SIZE) ? pos : NULL;
}

static void *pr_debug_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	if (*pos >= DEBUG_HASH_TABLE_SIZE)
		return NULL;
	return pos;
}

static void pr_debug_seq_stop(struct seq_file *s, void *v)
{
	/* Nothing to do */
}

static int pr_debug_seq_show(struct seq_file *s, void *v)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct debug_name *elem;
	unsigned int i = *(loff_t *) v;

	rcu_read_lock();
	head = &module_table[i];
	hlist_for_each_entry_rcu(elem, node, head, hlist) {
		seq_printf(s, "%s enabled=%d", elem->name, elem->enable);
		seq_printf(s, "\n");
	}
	rcu_read_unlock();
	return 0;
}

static struct seq_operations pr_debug_seq_ops = {
	.start = pr_debug_seq_start,
	.next  = pr_debug_seq_next,
	.stop  = pr_debug_seq_stop,
	.show  = pr_debug_seq_show
};

static int pr_debug_open(struct inode *inode, struct file *filp)
{
	return seq_open(filp, &pr_debug_seq_ops);
}

static const struct file_operations pr_debug_operations = {
	.open		= pr_debug_open,
	.read		= seq_read,
	.write		= pr_debug_write,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init dynamic_printk_init(void)
{
	struct dentry *dir, *file;
	struct mod_debug *iter;
	unsigned long value;

	dir = debugfs_create_dir("dynamic_printk", NULL);
	if (!dir)
		return -ENOMEM;
	file = debugfs_create_file("modules", 0644, dir, NULL,
					&pr_debug_operations);
	if (!file) {
		debugfs_remove(dir);
		return -ENOMEM;
	}
	for (value = (unsigned long)__start___verbose;
		value < (unsigned long)__stop___verbose;
		value += sizeof(struct mod_debug)) {
			iter = (struct mod_debug *)value;
			register_dynamic_debug_module(iter->modname,
				iter->type,
				iter->logical_modname,
				iter->flag_names, iter->hash, iter->hash2);
	}
	if (dynamic_enabled == DYNAMIC_ENABLED_ALL)
		set_all(true);
	return 0;
}
module_init(dynamic_printk_init);
/* may want to move this earlier so we can get traces as early as possible */

static int __init dynamic_printk_setup(char *str)
{
	if (str)
		return -ENOENT;
	dynamic_enabled = DYNAMIC_ENABLED_ALL;
	return 0;
}
/* Use early_param(), so we can get debug output as early as possible */
early_param("dynamic_printk", dynamic_printk_setup);
