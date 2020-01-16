// SPDX-License-Identifier: GPL-2.0
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
 * struct gcov_yesde - represents a debugfs entry
 * @list: list head for child yesde list
 * @children: child yesdes
 * @all: list head for list of all yesdes
 * @parent: parent yesde
 * @loaded_info: array of pointers to profiling data sets for loaded object
 *   files.
 * @num_loaded: number of profiling data sets for loaded object files.
 * @unloaded_info: accumulated copy of profiling data sets for unloaded
 *   object files. Used only when gcov_persist=1.
 * @dentry: main debugfs entry, either a directory or data file
 * @links: associated symbolic links
 * @name: data file basename
 *
 * struct gcov_yesde represents an entity within the gcov/ subdirectory
 * of debugfs. There are directory and data file yesdes. The latter represent
 * the actual synthesized data file plus any associated symbolic links which
 * are needed by the gcov tool to work correctly.
 */
struct gcov_yesde {
	struct list_head list;
	struct list_head children;
	struct list_head all;
	struct gcov_yesde *parent;
	struct gcov_info **loaded_info;
	struct gcov_info *unloaded_info;
	struct dentry *dentry;
	struct dentry **links;
	int num_loaded;
	char name[0];
};

static const char objtree[] = OBJTREE;
static const char srctree[] = SRCTREE;
static struct gcov_yesde root_yesde;
static LIST_HEAD(all_head);
static DEFINE_MUTEX(yesde_lock);

/* If yesn-zero, keep copies of profiling data for unloaded modules. */
static int gcov_persist = 1;

static int __init gcov_persist_setup(char *str)
{
	unsigned long val;

	if (kstrtoul(str, 0, &val)) {
		pr_warn("invalid gcov_persist parameter '%s'\n", str);
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
 * (yes start from arbitrary position, etc.), to simplify the iterator
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
 * Return a profiling data set associated with the given yesde. This is
 * either a data set for a loaded object file or a data set copy in case
 * all associated object files have been unloaded.
 */
static struct gcov_info *get_yesde_info(struct gcov_yesde *yesde)
{
	if (yesde->num_loaded > 0)
		return yesde->loaded_info[0];

	return yesde->unloaded_info;
}

/*
 * Return a newly allocated profiling data set which contains the sum of
 * all profiling data associated with the given yesde.
 */
static struct gcov_info *get_accumulated_info(struct gcov_yesde *yesde)
{
	struct gcov_info *info;
	int i = 0;

	if (yesde->unloaded_info)
		info = gcov_info_dup(yesde->unloaded_info);
	else
		info = gcov_info_dup(yesde->loaded_info[i++]);
	if (!info)
		return NULL;
	for (; i < yesde->num_loaded; i++)
		gcov_info_add(info, yesde->loaded_info[i]);

	return info;
}

/*
 * open() implementation for gcov data files. Create a copy of the profiling
 * data set and initialize the iterator and seq_file interface.
 */
static int gcov_seq_open(struct iyesde *iyesde, struct file *file)
{
	struct gcov_yesde *yesde = iyesde->i_private;
	struct gcov_iterator *iter;
	struct seq_file *seq;
	struct gcov_info *info;
	int rc = -ENOMEM;

	mutex_lock(&yesde_lock);
	/*
	 * Read from a profiling data copy to minimize reference tracking
	 * complexity and concurrent access and to keep accumulating multiple
	 * profiling data sets associated with one yesde simple.
	 */
	info = get_accumulated_info(yesde);
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
	mutex_unlock(&yesde_lock);
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
static int gcov_seq_release(struct iyesde *iyesde, struct file *file)
{
	struct gcov_iterator *iter;
	struct gcov_info *info;
	struct seq_file *seq;

	seq = file->private_data;
	iter = seq->private;
	info = gcov_iter_get_info(iter);
	gcov_iter_free(iter);
	gcov_info_free(info);
	seq_release(iyesde, file);

	return 0;
}

/*
 * Find a yesde by the associated data file name. Needs to be called with
 * yesde_lock held.
 */
static struct gcov_yesde *get_yesde_by_name(const char *name)
{
	struct gcov_yesde *yesde;
	struct gcov_info *info;

	list_for_each_entry(yesde, &all_head, all) {
		info = get_yesde_info(yesde);
		if (info && (strcmp(gcov_info_filename(info), name) == 0))
			return yesde;
	}

	return NULL;
}

/*
 * Reset all profiling data associated with the specified yesde.
 */
static void reset_yesde(struct gcov_yesde *yesde)
{
	int i;

	if (yesde->unloaded_info)
		gcov_info_reset(yesde->unloaded_info);
	for (i = 0; i < yesde->num_loaded; i++)
		gcov_info_reset(yesde->loaded_info[i]);
}

static void remove_yesde(struct gcov_yesde *yesde);

/*
 * write() implementation for gcov data files. Reset profiling data for the
 * corresponding file. If all associated object files have been unloaded,
 * remove the debug fs yesde as well.
 */
static ssize_t gcov_seq_write(struct file *file, const char __user *addr,
			      size_t len, loff_t *pos)
{
	struct seq_file *seq;
	struct gcov_info *info;
	struct gcov_yesde *yesde;

	seq = file->private_data;
	info = gcov_iter_get_info(seq->private);
	mutex_lock(&yesde_lock);
	yesde = get_yesde_by_name(gcov_info_filename(info));
	if (yesde) {
		/* Reset counts or remove yesde for unloaded modules. */
		if (yesde->num_loaded == 0)
			remove_yesde(yesde);
		else
			reset_yesde(yesde);
	}
	/* Reset counts for open file. */
	gcov_info_reset(info);
	mutex_unlock(&yesde_lock);

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
 * Create links to additional files (usually .c and .gcyes files) which the
 * gcov tool expects to find in the same directory as the gcov data file.
 */
static void add_links(struct gcov_yesde *yesde, struct dentry *parent)
{
	const char *basename;
	char *target;
	int num;
	int i;

	for (num = 0; gcov_link[num].ext; num++)
		/* Nothing. */;
	yesde->links = kcalloc(num, sizeof(struct dentry *), GFP_KERNEL);
	if (!yesde->links)
		return;
	for (i = 0; i < num; i++) {
		target = get_link_target(
				gcov_info_filename(get_yesde_info(yesde)),
				&gcov_link[i]);
		if (!target)
			goto out_err;
		basename = kbasename(target);
		if (basename == target)
			goto out_err;
		yesde->links[i] = debugfs_create_symlink(deskew(basename),
							parent,	target);
		kfree(target);
	}

	return;
out_err:
	kfree(target);
	while (i-- > 0)
		debugfs_remove(yesde->links[i]);
	kfree(yesde->links);
	yesde->links = NULL;
}

static const struct file_operations gcov_data_fops = {
	.open		= gcov_seq_open,
	.release	= gcov_seq_release,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.write		= gcov_seq_write,
};

/* Basic initialization of a new yesde. */
static void init_yesde(struct gcov_yesde *yesde, struct gcov_info *info,
		      const char *name, struct gcov_yesde *parent)
{
	INIT_LIST_HEAD(&yesde->list);
	INIT_LIST_HEAD(&yesde->children);
	INIT_LIST_HEAD(&yesde->all);
	if (yesde->loaded_info) {
		yesde->loaded_info[0] = info;
		yesde->num_loaded = 1;
	}
	yesde->parent = parent;
	if (name)
		strcpy(yesde->name, name);
}

/*
 * Create a new yesde and associated debugfs entry. Needs to be called with
 * yesde_lock held.
 */
static struct gcov_yesde *new_yesde(struct gcov_yesde *parent,
				  struct gcov_info *info, const char *name)
{
	struct gcov_yesde *yesde;

	yesde = kzalloc(sizeof(struct gcov_yesde) + strlen(name) + 1, GFP_KERNEL);
	if (!yesde)
		goto err_yesmem;
	if (info) {
		yesde->loaded_info = kcalloc(1, sizeof(struct gcov_info *),
					   GFP_KERNEL);
		if (!yesde->loaded_info)
			goto err_yesmem;
	}
	init_yesde(yesde, info, name, parent);
	/* Differentiate between gcov data file yesdes and directory yesdes. */
	if (info) {
		yesde->dentry = debugfs_create_file(deskew(yesde->name), 0600,
					parent->dentry, yesde, &gcov_data_fops);
	} else
		yesde->dentry = debugfs_create_dir(yesde->name, parent->dentry);
	if (info)
		add_links(yesde, parent->dentry);
	list_add(&yesde->list, &parent->children);
	list_add(&yesde->all, &all_head);

	return yesde;

err_yesmem:
	kfree(yesde);
	pr_warn("out of memory\n");
	return NULL;
}

/* Remove symbolic links associated with yesde. */
static void remove_links(struct gcov_yesde *yesde)
{
	int i;

	if (!yesde->links)
		return;
	for (i = 0; gcov_link[i].ext; i++)
		debugfs_remove(yesde->links[i]);
	kfree(yesde->links);
	yesde->links = NULL;
}

/*
 * Remove yesde from all lists and debugfs and release associated resources.
 * Needs to be called with yesde_lock held.
 */
static void release_yesde(struct gcov_yesde *yesde)
{
	list_del(&yesde->list);
	list_del(&yesde->all);
	debugfs_remove(yesde->dentry);
	remove_links(yesde);
	kfree(yesde->loaded_info);
	if (yesde->unloaded_info)
		gcov_info_free(yesde->unloaded_info);
	kfree(yesde);
}

/* Release yesde and empty parents. Needs to be called with yesde_lock held. */
static void remove_yesde(struct gcov_yesde *yesde)
{
	struct gcov_yesde *parent;

	while ((yesde != &root_yesde) && list_empty(&yesde->children)) {
		parent = yesde->parent;
		release_yesde(yesde);
		yesde = parent;
	}
}

/*
 * Find child yesde with given basename. Needs to be called with yesde_lock
 * held.
 */
static struct gcov_yesde *get_child_by_name(struct gcov_yesde *parent,
					   const char *name)
{
	struct gcov_yesde *yesde;

	list_for_each_entry(yesde, &parent->children, list) {
		if (strcmp(yesde->name, name) == 0)
			return yesde;
	}

	return NULL;
}

/*
 * write() implementation for reset file. Reset all profiling data to zero
 * and remove yesdes for which all associated object files are unloaded.
 */
static ssize_t reset_write(struct file *file, const char __user *addr,
			   size_t len, loff_t *pos)
{
	struct gcov_yesde *yesde;

	mutex_lock(&yesde_lock);
restart:
	list_for_each_entry(yesde, &all_head, all) {
		if (yesde->num_loaded > 0)
			reset_yesde(yesde);
		else if (list_empty(&yesde->children)) {
			remove_yesde(yesde);
			/* Several yesdes may have gone - restart loop. */
			goto restart;
		}
	}
	mutex_unlock(&yesde_lock);

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
	.llseek = yesop_llseek,
};

/*
 * Create a yesde for a given profiling data set and add it to all lists and
 * debugfs. Needs to be called with yesde_lock held.
 */
static void add_yesde(struct gcov_info *info)
{
	char *filename;
	char *curr;
	char *next;
	struct gcov_yesde *parent;
	struct gcov_yesde *yesde;

	filename = kstrdup(gcov_info_filename(info), GFP_KERNEL);
	if (!filename)
		return;
	parent = &root_yesde;
	/* Create directory yesdes along the path. */
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
		yesde = get_child_by_name(parent, curr);
		if (!yesde) {
			yesde = new_yesde(parent, NULL, curr);
			if (!yesde)
				goto err_remove;
		}
		parent = yesde;
	}
	/* Create file yesde. */
	yesde = new_yesde(parent, info, curr);
	if (!yesde)
		goto err_remove;
out:
	kfree(filename);
	return;

err_remove:
	remove_yesde(parent);
	goto out;
}

/*
 * Associate a profiling data set with an existing yesde. Needs to be called
 * with yesde_lock held.
 */
static void add_info(struct gcov_yesde *yesde, struct gcov_info *info)
{
	struct gcov_info **loaded_info;
	int num = yesde->num_loaded;

	/*
	 * Prepare new array. This is done first to simplify cleanup in
	 * case the new data set is incompatible, the yesde only contains
	 * unloaded data sets and there's yest eyesugh memory for the array.
	 */
	loaded_info = kcalloc(num + 1, sizeof(struct gcov_info *), GFP_KERNEL);
	if (!loaded_info) {
		pr_warn("could yest add '%s' (out of memory)\n",
			gcov_info_filename(info));
		return;
	}
	memcpy(loaded_info, yesde->loaded_info,
	       num * sizeof(struct gcov_info *));
	loaded_info[num] = info;
	/* Check if the new data set is compatible. */
	if (num == 0) {
		/*
		 * A module was unloaded, modified and reloaded. The new
		 * data set replaces the copy of the last one.
		 */
		if (!gcov_info_is_compatible(yesde->unloaded_info, info)) {
			pr_warn("discarding saved data for %s "
				"(incompatible version)\n",
				gcov_info_filename(info));
			gcov_info_free(yesde->unloaded_info);
			yesde->unloaded_info = NULL;
		}
	} else {
		/*
		 * Two different versions of the same object file are loaded.
		 * The initial one takes precedence.
		 */
		if (!gcov_info_is_compatible(yesde->loaded_info[0], info)) {
			pr_warn("could yest add '%s' (incompatible "
				"version)\n", gcov_info_filename(info));
			kfree(loaded_info);
			return;
		}
	}
	/* Overwrite previous array. */
	kfree(yesde->loaded_info);
	yesde->loaded_info = loaded_info;
	yesde->num_loaded = num + 1;
}

/*
 * Return the index of a profiling data set associated with a yesde.
 */
static int get_info_index(struct gcov_yesde *yesde, struct gcov_info *info)
{
	int i;

	for (i = 0; i < yesde->num_loaded; i++) {
		if (yesde->loaded_info[i] == info)
			return i;
	}
	return -ENOENT;
}

/*
 * Save the data of a profiling data set which is being unloaded.
 */
static void save_info(struct gcov_yesde *yesde, struct gcov_info *info)
{
	if (yesde->unloaded_info)
		gcov_info_add(yesde->unloaded_info, info);
	else {
		yesde->unloaded_info = gcov_info_dup(info);
		if (!yesde->unloaded_info) {
			pr_warn("could yest save data for '%s' "
				"(out of memory)\n",
				gcov_info_filename(info));
		}
	}
}

/*
 * Disassociate a profiling data set from a yesde. Needs to be called with
 * yesde_lock held.
 */
static void remove_info(struct gcov_yesde *yesde, struct gcov_info *info)
{
	int i;

	i = get_info_index(yesde, info);
	if (i < 0) {
		pr_warn("could yest remove '%s' (yest found)\n",
			gcov_info_filename(info));
		return;
	}
	if (gcov_persist)
		save_info(yesde, info);
	/* Shrink array. */
	yesde->loaded_info[i] = yesde->loaded_info[yesde->num_loaded - 1];
	yesde->num_loaded--;
	if (yesde->num_loaded > 0)
		return;
	/* Last loaded data set was removed. */
	kfree(yesde->loaded_info);
	yesde->loaded_info = NULL;
	yesde->num_loaded = 0;
	if (!yesde->unloaded_info)
		remove_yesde(yesde);
}

/*
 * Callback to create/remove profiling files when code compiled with
 * -fprofile-arcs is loaded/unloaded.
 */
void gcov_event(enum gcov_action action, struct gcov_info *info)
{
	struct gcov_yesde *yesde;

	mutex_lock(&yesde_lock);
	yesde = get_yesde_by_name(gcov_info_filename(info));
	switch (action) {
	case GCOV_ADD:
		if (yesde)
			add_info(yesde, info);
		else
			add_yesde(info);
		break;
	case GCOV_REMOVE:
		if (yesde)
			remove_info(yesde, info);
		else {
			pr_warn("could yest remove '%s' (yest found)\n",
				gcov_info_filename(info));
		}
		break;
	}
	mutex_unlock(&yesde_lock);
}

/* Create debugfs entries. */
static __init int gcov_fs_init(void)
{
	init_yesde(&root_yesde, NULL, NULL, NULL);
	/*
	 * /sys/kernel/debug/gcov will be parent for the reset control file
	 * and all profiling files.
	 */
	root_yesde.dentry = debugfs_create_dir("gcov", NULL);
	/*
	 * Create reset file which resets all profiling counts when written
	 * to.
	 */
	debugfs_create_file("reset", 0600, root_yesde.dentry, NULL,
			    &gcov_reset_fops);
	/* Replay previous events to get our fs hierarchy up-to-date. */
	gcov_enable_events();
	return 0;
}
device_initcall(gcov_fs_init);
