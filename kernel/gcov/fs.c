/*
 *  This code exports profiling data as debugfs files to userspace.
 *
 *    Copyright IBM Corp. 2009
 *    Author(s): Peter Oberparleiter <oberpar@linux.vnet.ibm.com>
 *
 *    Uses gcc-internal data definitions.
 *    Based on the gcov-kernel patch by:
 *		 Hubertus Franke <frankeh@us.ibm.com>
 *		 Nigel Hinds <nhinds@us.ibm.com>
 *		 Rajan Ravindran <rajancr@us.ibm.com>
 *		 Peter Oberparleiter <oberpar@linux.vnet.ibm.com>
 *		 Paul Larson
 *		 Yi CDL Yang
 */

#define pr_fmt(fmt)	"gcov: " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include "gcov.h"

/**
 * struct gcov_node - represents a debugfs entry
 * @list: list head for child node list
 * @children: child nodes
 * @all: list head for list of all nodes
 * @parent: parent node
 * @loaded_info: array of pointers to profiling data sets for loaded object
 *   files.
 * @num_loaded: number of profiling data sets for loaded object files.
 * @unloaded_info: accumulated copy of profiling data sets for unloaded
 *   object files. Used only when gcov_persist=1.
 * @dentry: main debugfs entry, either a directory or data file
 * @links: associated symbolic links
 * @name: data file basename
 *
 * struct gcov_node represents an entity within the gcov/ subdirectory
 * of debugfs. There are directory and data file nodes. The latter represent
 * the actual synthesized data file plus any associated symbolic links which
 * are needed by the gcov tool to work correctly.
 */
struct gcov_node {
	struct list_head list;
	struct list_head children;
	struct list_head all;
	struct gcov_node *parent;
	struct gcov_info **loaded_info;
	struct gcov_info *unloaded_info;
	struct dentry *dentry;
	struct dentry **links;
	int num_loaded;
	char name[0];
};

static const char objtree[] = OBJTREE;
static const char srctree[] = SRCTREE;
static struct gcov_node root_node;
static struct dentry *reset_dentry;
static LIST_HEAD(all_head);
static DEFINE_MUTEX(node_lock);

/* If non-zero, keep copies of profiling data for unloaded modules. */
static int gcov_persist = 1;

static int __init gcov_persist_setup(char *str)
{
	unsigned long val;

	if (strict_strtoul(str, 0, &val)) {
		pr_warning("invalid gcov_persist parameter '%s'\n", str);
		return 0;
	}
	gcov_persist = val;
	pr_info("setting gcov_persist to %d\n", gcov_persist);

	return 1;
}
__setup("gcov_persist=", gcov_persist_setup);

/*
 * seq_file.start() implementation for gcov data files. Note that the
 * gcov_iterator interface is designed to be more restrictive than seq_file
 * (no start from arbitrary position, etc.), to simplify the iterator
 * implementation.
 */
static void *gcov_seq_start(struct seq_file *seq, loff_t *pos)
{
	loff_t i;

	gcov_iter_start(seq->private);
	for (i = 0; i < *pos; i++) {
		if (gcov_iter_next(seq->private))
			return NULL;
	}
	return seq->private;
}

/* seq_file.next() implementation for gcov data files. */
static void *gcov_seq_next(struct seq_file *seq, void *data, loff_t *pos)
{
	struct gcov_iterator *iter = data;

	if (gcov_iter_next(iter))
		return NULL;
	(*pos)++;

	return iter;
}

/* seq_file.show() implementation for gcov data files. */
static int gcov_seq_show(struct seq_file *seq, void *data)
{
	struct gcov_iterator *iter = data;

	if (gcov_iter_write(iter, seq))
		return -EINVAL;
	return 0;
}

static void gcov_seq_stop(struct seq_file *seq, void *data)
{
	/* Unused. */
}

static const struct seq_operations gcov_seq_ops = {
	.start	= gcov_seq_start,
	.next	= gcov_seq_next,
	.show	= gcov_seq_show,
	.stop	= gcov_seq_stop,
};

/*
 * Return a profiling data set associated with the given node. This is
 * either a data set for a loaded object file or a data set copy in case
 * all associated object files have been unloaded.
 */
static struct gcov_info *get_node_info(struct gcov_node *node)
{
	if (node->num_loaded > 0)
		return node->loaded_info[0];

	return node->unloaded_info;
}

/*
 * Return a newly allocated profiling data set which contains the sum of
 * all profiling data associated with the given node.
 */
static struct gcov_info *get_accumulated_info(struct gcov_node *node)
{
	struct gcov_info *info;
	int i = 0;

	if (node->unloaded_info)
		info = gcov_info_dup(node->unloaded_info);
	else
		info = gcov_info_dup(node->loaded_info[i++]);
	if (!info)
		return NULL;
	for (; i < node->num_loaded; i++)
		gcov_info_add(info, node->loaded_info[i]);

	return info;
}

/*
 * open() implementation for gcov data files. Create a copy of the profiling
 * data set and initialize the iterator and seq_file interface.
 */
static int gcov_seq_open(struct inode *inode, struct file *file)
{
	struct gcov_node *node = inode->i_private;
	struct gcov_iterator *iter;
	struct seq_file *seq;
	struct gcov_info *info;
	int rc = -ENOMEM;

	mutex_lock(&node_lock);
	/*
	 * Read from a profiling data copy to minimize reference tracking
	 * complexity and concurrent access and to keep accumulating multiple
	 * profiling data sets associated with one node simple.
	 */
	info = get_accumulated_info(node);
	if (!info)
		goto out_unlock;
	iter = gcov_iter_new(info);
	if (!iter)
		goto err_free_info;
	rc = seq_open(file, &gcov_seq_ops);
	if (rc)
		goto err_free_iter_info;
	seq = file->private_data;
	seq->private = iter;
out_unlock:
	mutex_unlock(&node_lock);
	return rc;

err_free_iter_info:
	gcov_iter_free(iter);
err_free_info:
	gcov_info_free(info);
	goto out_unlock;
}

/*
 * release() implementation for gcov data files. Release resources allocated
 * by open().
 */
static int gcov_seq_release(struct inode *inode, struct file *file)
{
	struct gcov_iterator *iter;
	struct gcov_info *info;
	struct seq_file *seq;

	seq = file->private_data;
	iter = seq->private;
	info = gcov_iter_get_info(iter);
	gcov_iter_free(iter);
	gcov_info_free(info);
	seq_release(inode, file);

	return 0;
}

/*
 * Find a node by the associated data file name. Needs to be called with
 * node_lock held.
 */
static struct gcov_node *get_node_by_name(const char *name)
{
	struct gcov_node *node;
	struct gcov_info *info;

	list_for_each_entry(node, &all_head, all) {
		info = get_node_info(node);
		if (info && (strcmp(info->filename, name) == 0))
			return node;
	}

	return NULL;
}

/*
 * Reset all profiling data associated with the specified node.
 */
static void reset_node(struct gcov_node *node)
{
	int i;

	if (node->unloaded_info)
		gcov_info_reset(node->unloaded_info);
	for (i = 0; i < node->num_loaded; i++)
		gcov_info_reset(node->loaded_info[i]);
}

static void remove_node(struct gcov_node *node);

/*
 * write() implementation for gcov data files. Reset profiling data for the
 * corresponding file. If all associated object files have been unloaded,
 * remove the debug fs node as well.
 */
static ssize_t gcov_seq_write(struct file *file, const char __user *addr,
			      size_t len, loff_t *pos)
{
	struct seq_file *seq;
	struct gcov_info *info;
	struct gcov_node *node;

	seq = file->private_data;
	info = gcov_iter_get_info(seq->private);
	mutex_lock(&node_lock);
	node = get_node_by_name(info->filename);
	if (node) {
		/* Reset counts or remove node for unloaded modules. */
		if (node->num_loaded == 0)
			remove_node(node);
		else
			reset_node(node);
	}
	/* Reset counts for open file. */
	gcov_info_reset(info);
	mutex_unlock(&node_lock);

	return len;
}

/*
 * Given a string <path> representing a file path of format:
 *   path/to/file.gcda
 * construct and return a new string:
 *   <dir/>path/to/file.<ext>
 */
static char *link_target(const char *dir, const char *path, const char *ext)
{
	char *target;
	char *old_ext;
	char *copy;

	copy = kstrdup(path, GFP_KERNEL);
	if (!copy)
		return NULL;
	old_ext = strrchr(copy, '.');
	if (old_ext)
		*old_ext = '\0';
	if (dir)
		target = kasprintf(GFP_KERNEL, "%s/%s.%s", dir, copy, ext);
	else
		target = kasprintf(GFP_KERNEL, "%s.%s", copy, ext);
	kfree(copy);

	return target;
}

/*
 * Construct a string representing the symbolic link target for the given
 * gcov data file name and link type. Depending on the link type and the
 * location of the data file, the link target can either point to a
 * subdirectory of srctree, objtree or in an external location.
 */
static char *get_link_target(const char *filename, const struct gcov_link *ext)
{
	const char *rel;
	char *result;

	if (strncmp(filename, objtree, strlen(objtree)) == 0) {
		rel = filename + strlen(objtree) + 1;
		if (ext->dir == SRC_TREE)
			result = link_target(srctree, rel, ext->ext);
		else
			result = link_target(objtree, rel, ext->ext);
	} else {
		/* External compilation. */
		result = link_target(NULL, filename, ext->ext);
	}

	return result;
}

#define SKEW_PREFIX	".tmp_"

/*
 * For a filename .tmp_filename.ext return filename.ext. Needed to compensate
 * for filename skewing caused by the mod-versioning mechanism.
 */
static const char *deskew(const char *basename)
{
	if (strncmp(basename, SKEW_PREFIX, sizeof(SKEW_PREFIX) - 1) == 0)
		return basename + sizeof(SKEW_PREFIX) - 1;
	return basename;
}

/*
 * Create links to additional files (usually .c and .gcno files) which the
 * gcov tool expects to find in the same directory as the gcov data file.
 */
static void add_links(struct gcov_node *node, struct dentry *parent)
{
	char *basename;
	char *target;
	int num;
	int i;

	for (num = 0; gcov_link[num].ext; num++)
		/* Nothing. */;
	node->links = kcalloc(num, sizeof(struct dentry *), GFP_KERNEL);
	if (!node->links)
		return;
	for (i = 0; i < num; i++) {
		target = get_link_target(get_node_info(node)->filename,
					 &gcov_link[i]);
		if (!target)
			goto out_err;
		basename = strrchr(target, '/');
		if (!basename)
			goto out_err;
		basename++;
		node->links[i] = debugfs_create_symlink(deskew(basename),
							parent,	target);
		if (!node->links[i])
			goto out_err;
		kfree(target);
	}

	return;
out_err:
	kfree(target);
	while (i-- > 0)
		debugfs_remove(node->links[i]);
	kfree(node->links);
	node->links = NULL;
}

static const struct file_operations gcov_data_fops = {
	.open		= gcov_seq_open,
	.release	= gcov_seq_release,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.write		= gcov_seq_write,
};

/* Basic initialization of a new node. */
static void init_node(struct gcov_node *node, struct gcov_info *info,
		      const char *name, struct gcov_node *parent)
{
	INIT_LIST_HEAD(&node->list);
	INIT_LIST_HEAD(&node->children);
	INIT_LIST_HEAD(&node->all);
	if (node->loaded_info) {
		node->loaded_info[0] = info;
		node->num_loaded = 1;
	}
	node->parent = parent;
	if (name)
		strcpy(node->name, name);
}

/*
 * Create a new node and associated debugfs entry. Needs to be called with
 * node_lock held.
 */
static struct gcov_node *new_node(struct gcov_node *parent,
				  struct gcov_info *info, const char *name)
{
	struct gcov_node *node;

	node = kzalloc(sizeof(struct gcov_node) + strlen(name) + 1, GFP_KERNEL);
	if (!node)
		goto err_nomem;
	if (info) {
		node->loaded_info = kcalloc(1, sizeof(struct gcov_info *),
					   GFP_KERNEL);
		if (!node->loaded_info)
			goto err_nomem;
	}
	init_node(node, info, name, parent);
	/* Differentiate between gcov data file nodes and directory nodes. */
	if (info) {
		node->dentry = debugfs_create_file(deskew(node->name), 0600,
					parent->dentry, node, &gcov_data_fops);
	} else
		node->dentry = debugfs_create_dir(node->name, parent->dentry);
	if (!node->dentry) {
		pr_warning("could not create file\n");
		kfree(node);
		return NULL;
	}
	if (info)
		add_links(node, parent->dentry);
	list_add(&node->list, &parent->children);
	list_add(&node->all, &all_head);

	return node;

err_nomem:
	kfree(node);
	pr_warning("out of memory\n");
	return NULL;
}

/* Remove symbolic links associated with node. */
static void remove_links(struct gcov_node *node)
{
	int i;

	if (!node->links)
		return;
	for (i = 0; gcov_link[i].ext; i++)
		debugfs_remove(node->links[i]);
	kfree(node->links);
	node->links = NULL;
}

/*
 * Remove node from all lists and debugfs and release associated resources.
 * Needs to be called with node_lock held.
 */
static void release_node(struct gcov_node *node)
{
	list_del(&node->list);
	list_del(&node->all);
	debugfs_remove(node->dentry);
	remove_links(node);
	kfree(node->loaded_info);
	if (node->unloaded_info)
		gcov_info_free(node->unloaded_info);
	kfree(node);
}

/* Release node and empty parents. Needs to be called with node_lock held. */
static void remove_node(struct gcov_node *node)
{
	struct gcov_node *parent;

	while ((node != &root_node) && list_empty(&node->children)) {
		parent = node->parent;
		release_node(node);
		node = parent;
	}
}

/*
 * Find child node with given basename. Needs to be called with node_lock
 * held.
 */
static struct gcov_node *get_child_by_name(struct gcov_node *parent,
					   const char *name)
{
	struct gcov_node *node;

	list_for_each_entry(node, &parent->children, list) {
		if (strcmp(node->name, name) == 0)
			return node;
	}

	return NULL;
}

/*
 * write() implementation for reset file. Reset all profiling data to zero
 * and remove nodes for which all associated object files are unloaded.
 */
static ssize_t reset_write(struct file *file, const char __user *addr,
			   size_t len, loff_t *pos)
{
	struct gcov_node *node;

	mutex_lock(&node_lock);
restart:
	list_for_each_entry(node, &all_head, all) {
		if (node->num_loaded > 0)
			reset_node(node);
		else if (list_empty(&node->children)) {
			remove_node(node);
			/* Several nodes may have gone - restart loop. */
			goto restart;
		}
	}
	mutex_unlock(&node_lock);

	return len;
}

/* read() implementation for reset file. Unused. */
static ssize_t reset_read(struct file *file, char __user *addr, size_t len,
			  loff_t *pos)
{
	/* Allow read operation so that a recursive copy won't fail. */
	return 0;
}

static const struct file_operations gcov_reset_fops = {
	.write	= reset_write,
	.read	= reset_read,
};

/*
 * Create a node for a given profiling data set and add it to all lists and
 * debugfs. Needs to be called with node_lock held.
 */
static void add_node(struct gcov_info *info)
{
	char *filename;
	char *curr;
	char *next;
	struct gcov_node *parent;
	struct gcov_node *node;

	filename = kstrdup(info->filename, GFP_KERNEL);
	if (!filename)
		return;
	parent = &root_node;
	/* Create directory nodes along the path. */
	for (curr = filename; (next = strchr(curr, '/')); curr = next + 1) {
		if (curr == next)
			continue;
		*next = 0;
		if (strcmp(curr, ".") == 0)
			continue;
		if (strcmp(curr, "..") == 0) {
			if (!parent->parent)
				goto err_remove;
			parent = parent->parent;
			continue;
		}
		node = get_child_by_name(parent, curr);
		if (!node) {
			node = new_node(parent, NULL, curr);
			if (!node)
				goto err_remove;
		}
		parent = node;
	}
	/* Create file node. */
	node = new_node(parent, info, curr);
	if (!node)
		goto err_remove;
out:
	kfree(filename);
	return;

err_remove:
	remove_node(parent);
	goto out;
}

/*
 * Associate a profiling data set with an existing node. Needs to be called
 * with node_lock held.
 */
static void add_info(struct gcov_node *node, struct gcov_info *info)
{
	struct gcov_info **loaded_info;
	int num = node->num_loaded;

	/*
	 * Prepare new array. This is done first to simplify cleanup in
	 * case the new data set is incompatible, the node only contains
	 * unloaded data sets and there's not enough memory for the array.
	 */
	loaded_info = kcalloc(num + 1, sizeof(struct gcov_info *), GFP_KERNEL);
	if (!loaded_info) {
		pr_warning("could not add '%s' (out of memory)\n",
			   info->filename);
		return;
	}
	memcpy(loaded_info, node->loaded_info,
	       num * sizeof(struct gcov_info *));
	loaded_info[num] = info;
	/* Check if the new data set is compatible. */
	if (num == 0) {
		/*
		 * A module was unloaded, modified and reloaded. The new
		 * data set replaces the copy of the last one.
		 */
		if (!gcov_info_is_compatible(node->unloaded_info, info)) {
			pr_warning("discarding saved data for %s "
				   "(incompatible version)\n", info->filename);
			gcov_info_free(node->unloaded_info);
			node->unloaded_info = NULL;
		}
	} else {
		/*
		 * Two different versions of the same object file are loaded.
		 * The initial one takes precedence.
		 */
		if (!gcov_info_is_compatible(node->loaded_info[0], info)) {
			pr_warning("could not add '%s' (incompatible "
				   "version)\n", info->filename);
			kfree(loaded_info);
			return;
		}
	}
	/* Overwrite previous array. */
	kfree(node->loaded_info);
	node->loaded_info = loaded_info;
	node->num_loaded = num + 1;
}

/*
 * Return the index of a profiling data set associated with a node.
 */
static int get_info_index(struct gcov_node *node, struct gcov_info *info)
{
	int i;

	for (i = 0; i < node->num_loaded; i++) {
		if (node->loaded_info[i] == info)
			return i;
	}
	return -ENOENT;
}

/*
 * Save the data of a profiling data set which is being unloaded.
 */
static void save_info(struct gcov_node *node, struct gcov_info *info)
{
	if (node->unloaded_info)
		gcov_info_add(node->unloaded_info, info);
	else {
		node->unloaded_info = gcov_info_dup(info);
		if (!node->unloaded_info) {
			pr_warning("could not save data for '%s' "
				   "(out of memory)\n", info->filename);
		}
	}
}

/*
 * Disassociate a profiling data set from a node. Needs to be called with
 * node_lock held.
 */
static void remove_info(struct gcov_node *node, struct gcov_info *info)
{
	int i;

	i = get_info_index(node, info);
	if (i < 0) {
		pr_warning("could not remove '%s' (not found)\n",
			   info->filename);
		return;
	}
	if (gcov_persist)
		save_info(node, info);
	/* Shrink array. */
	node->loaded_info[i] = node->loaded_info[node->num_loaded - 1];
	node->num_loaded--;
	if (node->num_loaded > 0)
		return;
	/* Last loaded data set was removed. */
	kfree(node->loaded_info);
	node->loaded_info = NULL;
	node->num_loaded = 0;
	if (!node->unloaded_info)
		remove_node(node);
}

/*
 * Callback to create/remove profiling files when code compiled with
 * -fprofile-arcs is loaded/unloaded.
 */
void gcov_event(enum gcov_action action, struct gcov_info *info)
{
	struct gcov_node *node;

	mutex_lock(&node_lock);
	node = get_node_by_name(info->filename);
	switch (action) {
	case GCOV_ADD:
		if (node)
			add_info(node, info);
		else
			add_node(info);
		break;
	case GCOV_REMOVE:
		if (node)
			remove_info(node, info);
		else {
			pr_warning("could not remove '%s' (not found)\n",
				   info->filename);
		}
		break;
	}
	mutex_unlock(&node_lock);
}

/* Create debugfs entries. */
static __init int gcov_fs_init(void)
{
	int rc = -EIO;

	init_node(&root_node, NULL, NULL, NULL);
	/*
	 * /sys/kernel/debug/gcov will be parent for the reset control file
	 * and all profiling files.
	 */
	root_node.dentry = debugfs_create_dir("gcov", NULL);
	if (!root_node.dentry)
		goto err_remove;
	/*
	 * Create reset file which resets all profiling counts when written
	 * to.
	 */
	reset_dentry = debugfs_create_file("reset", 0600, root_node.dentry,
					   NULL, &gcov_reset_fops);
	if (!reset_dentry)
		goto err_remove;
	/* Replay previous events to get our fs hierarchy up-to-date. */
	gcov_enable_events();
	return 0;

err_remove:
	pr_err("init failed\n");
	if (root_node.dentry)
		debugfs_remove(root_node.dentry);

	return rc;
}
device_initcall(gcov_fs_init);
