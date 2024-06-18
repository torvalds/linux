// SPDX-License-Identifier: GPL-2.0
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/shrinker.h>
#include <linux/memcontrol.h>

#include "internal.h"

/* defined in vmscan.c */
extern struct mutex shrinker_mutex;
extern struct list_head shrinker_list;

static DEFINE_IDA(shrinker_debugfs_ida);
static struct dentry *shrinker_debugfs_root;

static unsigned long shrinker_count_objects(struct shrinker *shrinker,
					    struct mem_cgroup *memcg,
					    unsigned long *count_per_node)
{
	unsigned long nr, total = 0;
	int nid;

	for_each_node(nid) {
		if (nid == 0 || (shrinker->flags & SHRINKER_NUMA_AWARE)) {
			struct shrink_control sc = {
				.gfp_mask = GFP_KERNEL,
				.nid = nid,
				.memcg = memcg,
			};

			nr = shrinker->count_objects(shrinker, &sc);
			if (nr == SHRINK_EMPTY)
				nr = 0;
		} else {
			nr = 0;
		}

		count_per_node[nid] = nr;
		total += nr;
	}

	return total;
}

static int shrinker_debugfs_count_show(struct seq_file *m, void *v)
{
	struct shrinker *shrinker = m->private;
	unsigned long *count_per_node;
	struct mem_cgroup *memcg;
	unsigned long total;
	bool memcg_aware;
	int ret = 0, nid;

	count_per_node = kcalloc(nr_node_ids, sizeof(unsigned long), GFP_KERNEL);
	if (!count_per_node)
		return -ENOMEM;

	rcu_read_lock();

	memcg_aware = shrinker->flags & SHRINKER_MEMCG_AWARE;

	memcg = mem_cgroup_iter(NULL, NULL, NULL);
	do {
		if (memcg && !mem_cgroup_online(memcg))
			continue;

		total = shrinker_count_objects(shrinker,
					       memcg_aware ? memcg : NULL,
					       count_per_node);
		if (total) {
			seq_printf(m, "%lu", mem_cgroup_ino(memcg));
			for_each_node(nid)
				seq_printf(m, " %lu", count_per_node[nid]);
			seq_putc(m, '\n');
		}

		if (!memcg_aware) {
			mem_cgroup_iter_break(NULL, memcg);
			break;
		}

		if (signal_pending(current)) {
			mem_cgroup_iter_break(NULL, memcg);
			ret = -EINTR;
			break;
		}
	} while ((memcg = mem_cgroup_iter(NULL, memcg, NULL)) != NULL);

	rcu_read_unlock();

	kfree(count_per_node);
	return ret;
}
DEFINE_SHOW_ATTRIBUTE(shrinker_debugfs_count);

static int shrinker_debugfs_scan_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return nonseekable_open(inode, file);
}

static ssize_t shrinker_debugfs_scan_write(struct file *file,
					   const char __user *buf,
					   size_t size, loff_t *pos)
{
	struct shrinker *shrinker = file->private_data;
	unsigned long nr_to_scan = 0, ino, read_len;
	struct shrink_control sc = {
		.gfp_mask = GFP_KERNEL,
	};
	struct mem_cgroup *memcg = NULL;
	int nid;
	char kbuf[72];

	read_len = size < (sizeof(kbuf) - 1) ? size : (sizeof(kbuf) - 1);
	if (copy_from_user(kbuf, buf, read_len))
		return -EFAULT;
	kbuf[read_len] = '\0';

	if (sscanf(kbuf, "%lu %d %lu", &ino, &nid, &nr_to_scan) != 3)
		return -EINVAL;

	if (nid < 0 || nid >= nr_node_ids)
		return -EINVAL;

	if (nr_to_scan == 0)
		return size;

	if (shrinker->flags & SHRINKER_MEMCG_AWARE) {
		memcg = mem_cgroup_get_from_ino(ino);
		if (!memcg || IS_ERR(memcg))
			return -ENOENT;

		if (!mem_cgroup_online(memcg)) {
			mem_cgroup_put(memcg);
			return -ENOENT;
		}
	} else if (ino != 0) {
		return -EINVAL;
	}

	sc.nid = nid;
	sc.memcg = memcg;
	sc.nr_to_scan = nr_to_scan;
	sc.nr_scanned = nr_to_scan;

	shrinker->scan_objects(shrinker, &sc);

	mem_cgroup_put(memcg);

	return size;
}

static const struct file_operations shrinker_debugfs_scan_fops = {
	.owner	 = THIS_MODULE,
	.open	 = shrinker_debugfs_scan_open,
	.write	 = shrinker_debugfs_scan_write,
};

int shrinker_debugfs_add(struct shrinker *shrinker)
{
	struct dentry *entry;
	char buf[128];
	int id;

	lockdep_assert_held(&shrinker_mutex);

	/* debugfs isn't initialized yet, add debugfs entries later. */
	if (!shrinker_debugfs_root)
		return 0;

	id = ida_alloc(&shrinker_debugfs_ida, GFP_KERNEL);
	if (id < 0)
		return id;
	shrinker->debugfs_id = id;

	snprintf(buf, sizeof(buf), "%s-%d", shrinker->name, id);

	/* create debugfs entry */
	entry = debugfs_create_dir(buf, shrinker_debugfs_root);
	if (IS_ERR(entry)) {
		ida_free(&shrinker_debugfs_ida, id);
		return PTR_ERR(entry);
	}
	shrinker->debugfs_entry = entry;

	debugfs_create_file("count", 0440, entry, shrinker,
			    &shrinker_debugfs_count_fops);
	debugfs_create_file("scan", 0220, entry, shrinker,
			    &shrinker_debugfs_scan_fops);
	return 0;
}

int shrinker_debugfs_rename(struct shrinker *shrinker, const char *fmt, ...)
{
	struct dentry *entry;
	char buf[128];
	const char *new, *old;
	va_list ap;
	int ret = 0;

	va_start(ap, fmt);
	new = kvasprintf_const(GFP_KERNEL, fmt, ap);
	va_end(ap);

	if (!new)
		return -ENOMEM;

	mutex_lock(&shrinker_mutex);

	old = shrinker->name;
	shrinker->name = new;

	if (shrinker->debugfs_entry) {
		snprintf(buf, sizeof(buf), "%s-%d", shrinker->name,
			 shrinker->debugfs_id);

		entry = debugfs_rename(shrinker_debugfs_root,
				       shrinker->debugfs_entry,
				       shrinker_debugfs_root, buf);
		if (IS_ERR(entry))
			ret = PTR_ERR(entry);
		else
			shrinker->debugfs_entry = entry;
	}

	mutex_unlock(&shrinker_mutex);

	kfree_const(old);

	return ret;
}
EXPORT_SYMBOL(shrinker_debugfs_rename);

struct dentry *shrinker_debugfs_detach(struct shrinker *shrinker,
				       int *debugfs_id)
{
	struct dentry *entry = shrinker->debugfs_entry;

	lockdep_assert_held(&shrinker_mutex);

	*debugfs_id = entry ? shrinker->debugfs_id : -1;
	shrinker->debugfs_entry = NULL;

	return entry;
}

void shrinker_debugfs_remove(struct dentry *debugfs_entry, int debugfs_id)
{
	debugfs_remove_recursive(debugfs_entry);
	ida_free(&shrinker_debugfs_ida, debugfs_id);
}

static int __init shrinker_debugfs_init(void)
{
	struct shrinker *shrinker;
	struct dentry *dentry;
	int ret = 0;

	dentry = debugfs_create_dir("shrinker", NULL);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	shrinker_debugfs_root = dentry;

	/* Create debugfs entries for shrinkers registered at boot */
	mutex_lock(&shrinker_mutex);
	list_for_each_entry(shrinker, &shrinker_list, list)
		if (!shrinker->debugfs_entry) {
			ret = shrinker_debugfs_add(shrinker);
			if (ret)
				break;
		}
	mutex_unlock(&shrinker_mutex);

	return ret;
}
late_initcall(shrinker_debugfs_init);
