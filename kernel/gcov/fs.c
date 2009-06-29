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
 * @info: associated profiling data structure if not a directory
 * @ghost: when an object file containing profiling data is unloaded we keep a
 *         copy of the profiling data here to allow collecting coverage data
 *         for cleanup code. Such a node is called a "ghost".
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
	struct gcov_info *info;
	struct gcov_info *ghost;
	struct dentry *dentry;
	struct dentry **links;
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
 * Return the profiling data set for a given node. This can either be the
 * original profiling data structure or a duplicate (also called "ghost")
 * in case the associated object file has been unloaded.
 */
static struct gcov_info *get_node_info(struct gcov_node *node)
{
	if (node->info)
		return node->info;

	return node->ghost;
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
	 * complexity and concurrent access.
	 */
	info = gcov_info_dup(get_node_info(node));
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

static void remove_node(struct gcov_node *node);

/*
 * write() implementation for gcov data files. Reset profiling data for the
 * associated file. If the object file has been unloaded (i.e. this is
 * a "ghost" node), remove the debug fs node as well.
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
		if (node->ghost)
			remove_node(node);
		else
			gcov_info_reset(node->info);
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
	node->info = info;
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
	if (!node) {
		pr_warning("out of memory\n");
		return NULL;
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
	if (node->ghost)
		gcov_info_free(node->ghost);
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
 * and remove ghost nodes.
 */
static ssize_t reset_write(struct file *file, const char __user *addr,
			   size_t len, loff_t *pos)
{
	struct gcov_node *node;

	mutex_lock(&node_lock);
restart:
	list_for_each_entry(node, &all_head, all) {
		if (node->info)
			gcov_info_reset(node->info);
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
 * The profiling data set associated with this node is being unloaded. Store a
 * copy of the profiling data and turn this node into a "ghost".
 */
static int ghost_node(struct gcov_node *node)
{
	node->ghost = gcov_info_dup(node->info);
	if (!node->ghost) {
		pr_warning("could not save data for '%s' (out of memory)\n",
			   node->info->filename);
		return -ENOMEM;
	}
	node->info = NULL;

	return 0;
}

/*
 * Profiling data for this node has been loaded again. Add profiling data
 * from previous instantiation and turn this node into a regular node.
 */
static void revive_node(struct gcov_node *node, struct gcov_info *info)
{
	if (gcov_info_is_compatible(node->ghost, info))
		gcov_info_add(info, node->ghost);
	else {
		pr_warning("discarding saved data for '%s' (version changed)\n",
			   info->filename);
	}
	gcov_info_free(node->ghost);
	node->ghost = NULL;
	node->info = info;
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
		/* Add new node or revive ghost. */
		if (!node) {
			add_node(info);
			break;
		}
		if (gcov_persist)
			revive_node(node, info);
		else {
			pr_warning("could not add '%s' (already exists)\n",
				   info->filename);
		}
		break;
	case GCOV_REMOVE:
		/* Remove node or turn into ghost. */
		if (!node) {
			pr_warning("could not remove '%s' (not found)\n",
				   info->filename);
			break;
		}
		if (gcov_persist) {
			if (!ghost_node(node))
				break;
		}
		remove_node(node);
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
