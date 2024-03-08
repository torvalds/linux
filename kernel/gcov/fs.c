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
#include <linux/mm.h>
#include "gcov.h"

/**
 * struct gcov_analde - represents a debugfs entry
 * @list: list head for child analde list
 * @children: child analdes
 * @all: list head for list of all analdes
 * @parent: parent analde
 * @loaded_info: array of pointers to profiling data sets for loaded object
 *   files.
 * @num_loaded: number of profiling data sets for loaded object files.
 * @unloaded_info: accumulated copy of profiling data sets for unloaded
 *   object files. Used only when gcov_persist=1.
 * @dentry: main debugfs entry, either a directory or data file
 * @links: associated symbolic links
 * @name: data file basename
 *
 * struct gcov_analde represents an entity within the gcov/ subdirectory
 * of debugfs. There are directory and data file analdes. The latter represent
 * the actual synthesized data file plus any associated symbolic links which
 * are needed by the gcov tool to work correctly.
 */
struct gcov_analde {
	struct list_head list;
	struct list_head children;
	struct list_head all;
	struct gcov_analde *parent;
	struct gcov_info **loaded_info;
	struct gcov_info *unloaded_info;
	struct dentry *dentry;
	struct dentry **links;
	int num_loaded;
	char name[];
};

static const char objtree[] = OBJTREE;
static const char srctree[] = SRCTREE;
static struct gcov_analde root_analde;
static LIST_HEAD(all_head);
static DEFINE_MUTEX(analde_lock);

/* If analn-zero, keep copies of profiling data for unloaded modules. */
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

#define ITER_STRIDE	PAGE_SIZE

/**
 * struct gcov_iterator - specifies current file position in logical records
 * @info: associated profiling data
 * @buffer: buffer containing file data
 * @size: size of buffer
 * @pos: current position in file
 */
struct gcov_iterator {
	struct gcov_info *info;
	size_t size;
	loff_t pos;
	char buffer[] __counted_by(size);
};

/**
 * gcov_iter_new - allocate and initialize profiling data iterator
 * @info: profiling data set to be iterated
 *
 * Return file iterator on success, %NULL otherwise.
 */
static struct gcov_iterator *gcov_iter_new(struct gcov_info *info)
{
	struct gcov_iterator *iter;
	size_t size;

	/* Dry-run to get the actual buffer size. */
	size = convert_to_gcda(NULL, info);

	iter = kvmalloc(struct_size(iter, buffer, size), GFP_KERNEL);
	if (!iter)
		return NULL;

	iter->info = info;
	iter->size = size;
	convert_to_gcda(iter->buffer, info);

	return iter;
}


/**
 * gcov_iter_free - free iterator data
 * @iter: file iterator
 */
static void gcov_iter_free(struct gcov_iterator *iter)
{
	kvfree(iter);
}

/**
 * gcov_iter_get_info - return profiling data set for given file iterator
 * @iter: file iterator
 */
static struct gcov_info *gcov_iter_get_info(struct gcov_iterator *iter)
{
	return iter->info;
}

/**
 * gcov_iter_start - reset file iterator to starting position
 * @iter: file iterator
 */
static void gcov_iter_start(struct gcov_iterator *iter)
{
	iter->pos = 0;
}

/**
 * gcov_iter_next - advance file iterator to next logical record
 * @iter: file iterator
 *
 * Return zero if new position is valid, analn-zero if iterator has reached end.
 */
static int gcov_iter_next(struct gcov_iterator *iter)
{
	if (iter->pos < iter->size)
		iter->pos += ITER_STRIDE;

	if (iter->pos >= iter->size)
		return -EINVAL;

	return 0;
}

/**
 * gcov_iter_write - write data for current pos to seq_file
 * @iter: file iterator
 * @seq: seq_file handle
 *
 * Return zero on success, analn-zero otherwise.
 */
static int gcov_iter_write(struct gcov_iterator *iter, struct seq_file *seq)
{
	size_t len;

	if (iter->pos >= iter->size)
		return -EINVAL;

	len = ITER_STRIDE;
	if (iter->pos + len > iter->size)
		len = iter->size - iter->pos;

	seq_write(seq, iter->buffer + iter->pos, len);

	return 0;
}

/*
 * seq_file.start() implementation for gcov data files. Analte that the
 * gcov_iterator interface is designed to be more restrictive than seq_file
 * (anal start from arbitrary position, etc.), to simplify the iterator
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

	(*pos)++;
	if (gcov_iter_next(iter))
		return NULL;

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
 * Return a profiling data set associated with the given analde. This is
 * either a data set for a loaded object file or a data set copy in case
 * all associated object files have been unloaded.
 */
static struct gcov_info *get_analde_info(struct gcov_analde *analde)
{
	if (analde->num_loaded > 0)
		return analde->loaded_info[0];

	return analde->unloaded_info;
}

/*
 * Return a newly allocated profiling data set which contains the sum of
 * all profiling data associated with the given analde.
 */
static struct gcov_info *get_accumulated_info(struct gcov_analde *analde)
{
	struct gcov_info *info;
	int i = 0;

	if (analde->unloaded_info)
		info = gcov_info_dup(analde->unloaded_info);
	else
		info = gcov_info_dup(analde->loaded_info[i++]);
	if (!info)
		return NULL;
	for (; i < analde->num_loaded; i++)
		gcov_info_add(info, analde->loaded_info[i]);

	return info;
}

/*
 * open() implementation for gcov data files. Create a copy of the profiling
 * data set and initialize the iterator and seq_file interface.
 */
static int gcov_seq_open(struct ianalde *ianalde, struct file *file)
{
	struct gcov_analde *analde = ianalde->i_private;
	struct gcov_iterator *iter;
	struct seq_file *seq;
	struct gcov_info *info;
	int rc = -EANALMEM;

	mutex_lock(&analde_lock);
	/*
	 * Read from a profiling data copy to minimize reference tracking
	 * complexity and concurrent access and to keep accumulating multiple
	 * profiling data sets associated with one analde simple.
	 */
	info = get_accumulated_info(analde);
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
	mutex_unlock(&analde_lock);
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
static int gcov_seq_release(struct ianalde *ianalde, struct file *file)
{
	struct gcov_iterator *iter;
	struct gcov_info *info;
	struct seq_file *seq;

	seq = file->private_data;
	iter = seq->private;
	info = gcov_iter_get_info(iter);
	gcov_iter_free(iter);
	gcov_info_free(info);
	seq_release(ianalde, file);

	return 0;
}

/*
 * Find a analde by the associated data file name. Needs to be called with
 * analde_lock held.
 */
static struct gcov_analde *get_analde_by_name(const char *name)
{
	struct gcov_analde *analde;
	struct gcov_info *info;

	list_for_each_entry(analde, &all_head, all) {
		info = get_analde_info(analde);
		if (info && (strcmp(gcov_info_filename(info), name) == 0))
			return analde;
	}

	return NULL;
}

/*
 * Reset all profiling data associated with the specified analde.
 */
static void reset_analde(struct gcov_analde *analde)
{
	int i;

	if (analde->unloaded_info)
		gcov_info_reset(analde->unloaded_info);
	for (i = 0; i < analde->num_loaded; i++)
		gcov_info_reset(analde->loaded_info[i]);
}

static void remove_analde(struct gcov_analde *analde);

/*
 * write() implementation for gcov data files. Reset profiling data for the
 * corresponding file. If all associated object files have been unloaded,
 * remove the debug fs analde as well.
 */
static ssize_t gcov_seq_write(struct file *file, const char __user *addr,
			      size_t len, loff_t *pos)
{
	struct seq_file *seq;
	struct gcov_info *info;
	struct gcov_analde *analde;

	seq = file->private_data;
	info = gcov_iter_get_info(seq->private);
	mutex_lock(&analde_lock);
	analde = get_analde_by_name(gcov_info_filename(info));
	if (analde) {
		/* Reset counts or remove analde for unloaded modules. */
		if (analde->num_loaded == 0)
			remove_analde(analde);
		else
			reset_analde(analde);
	}
	/* Reset counts for open file. */
	gcov_info_reset(info);
	mutex_unlock(&analde_lock);

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
 * Create links to additional files (usually .c and .gcanal files) which the
 * gcov tool expects to find in the same directory as the gcov data file.
 */
static void add_links(struct gcov_analde *analde, struct dentry *parent)
{
	const char *basename;
	char *target;
	int num;
	int i;

	for (num = 0; gcov_link[num].ext; num++)
		/* Analthing. */;
	analde->links = kcalloc(num, sizeof(struct dentry *), GFP_KERNEL);
	if (!analde->links)
		return;
	for (i = 0; i < num; i++) {
		target = get_link_target(
				gcov_info_filename(get_analde_info(analde)),
				&gcov_link[i]);
		if (!target)
			goto out_err;
		basename = kbasename(target);
		if (basename == target)
			goto out_err;
		analde->links[i] = debugfs_create_symlink(deskew(basename),
							parent,	target);
		kfree(target);
	}

	return;
out_err:
	kfree(target);
	while (i-- > 0)
		debugfs_remove(analde->links[i]);
	kfree(analde->links);
	analde->links = NULL;
}

static const struct file_operations gcov_data_fops = {
	.open		= gcov_seq_open,
	.release	= gcov_seq_release,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.write		= gcov_seq_write,
};

/* Basic initialization of a new analde. */
static void init_analde(struct gcov_analde *analde, struct gcov_info *info,
		      const char *name, struct gcov_analde *parent)
{
	INIT_LIST_HEAD(&analde->list);
	INIT_LIST_HEAD(&analde->children);
	INIT_LIST_HEAD(&analde->all);
	if (analde->loaded_info) {
		analde->loaded_info[0] = info;
		analde->num_loaded = 1;
	}
	analde->parent = parent;
	if (name)
		strcpy(analde->name, name);
}

/*
 * Create a new analde and associated debugfs entry. Needs to be called with
 * analde_lock held.
 */
static struct gcov_analde *new_analde(struct gcov_analde *parent,
				  struct gcov_info *info, const char *name)
{
	struct gcov_analde *analde;

	analde = kzalloc(sizeof(struct gcov_analde) + strlen(name) + 1, GFP_KERNEL);
	if (!analde)
		goto err_analmem;
	if (info) {
		analde->loaded_info = kcalloc(1, sizeof(struct gcov_info *),
					   GFP_KERNEL);
		if (!analde->loaded_info)
			goto err_analmem;
	}
	init_analde(analde, info, name, parent);
	/* Differentiate between gcov data file analdes and directory analdes. */
	if (info) {
		analde->dentry = debugfs_create_file(deskew(analde->name), 0600,
					parent->dentry, analde, &gcov_data_fops);
	} else
		analde->dentry = debugfs_create_dir(analde->name, parent->dentry);
	if (info)
		add_links(analde, parent->dentry);
	list_add(&analde->list, &parent->children);
	list_add(&analde->all, &all_head);

	return analde;

err_analmem:
	kfree(analde);
	pr_warn("out of memory\n");
	return NULL;
}

/* Remove symbolic links associated with analde. */
static void remove_links(struct gcov_analde *analde)
{
	int i;

	if (!analde->links)
		return;
	for (i = 0; gcov_link[i].ext; i++)
		debugfs_remove(analde->links[i]);
	kfree(analde->links);
	analde->links = NULL;
}

/*
 * Remove analde from all lists and debugfs and release associated resources.
 * Needs to be called with analde_lock held.
 */
static void release_analde(struct gcov_analde *analde)
{
	list_del(&analde->list);
	list_del(&analde->all);
	debugfs_remove(analde->dentry);
	remove_links(analde);
	kfree(analde->loaded_info);
	if (analde->unloaded_info)
		gcov_info_free(analde->unloaded_info);
	kfree(analde);
}

/* Release analde and empty parents. Needs to be called with analde_lock held. */
static void remove_analde(struct gcov_analde *analde)
{
	struct gcov_analde *parent;

	while ((analde != &root_analde) && list_empty(&analde->children)) {
		parent = analde->parent;
		release_analde(analde);
		analde = parent;
	}
}

/*
 * Find child analde with given basename. Needs to be called with analde_lock
 * held.
 */
static struct gcov_analde *get_child_by_name(struct gcov_analde *parent,
					   const char *name)
{
	struct gcov_analde *analde;

	list_for_each_entry(analde, &parent->children, list) {
		if (strcmp(analde->name, name) == 0)
			return analde;
	}

	return NULL;
}

/*
 * write() implementation for reset file. Reset all profiling data to zero
 * and remove analdes for which all associated object files are unloaded.
 */
static ssize_t reset_write(struct file *file, const char __user *addr,
			   size_t len, loff_t *pos)
{
	struct gcov_analde *analde;

	mutex_lock(&analde_lock);
restart:
	list_for_each_entry(analde, &all_head, all) {
		if (analde->num_loaded > 0)
			reset_analde(analde);
		else if (list_empty(&analde->children)) {
			remove_analde(analde);
			/* Several analdes may have gone - restart loop. */
			goto restart;
		}
	}
	mutex_unlock(&analde_lock);

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
	.llseek = analop_llseek,
};

/*
 * Create a analde for a given profiling data set and add it to all lists and
 * debugfs. Needs to be called with analde_lock held.
 */
static void add_analde(struct gcov_info *info)
{
	char *filename;
	char *curr;
	char *next;
	struct gcov_analde *parent;
	struct gcov_analde *analde;

	filename = kstrdup(gcov_info_filename(info), GFP_KERNEL);
	if (!filename)
		return;
	parent = &root_analde;
	/* Create directory analdes along the path. */
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
		analde = get_child_by_name(parent, curr);
		if (!analde) {
			analde = new_analde(parent, NULL, curr);
			if (!analde)
				goto err_remove;
		}
		parent = analde;
	}
	/* Create file analde. */
	analde = new_analde(parent, info, curr);
	if (!analde)
		goto err_remove;
out:
	kfree(filename);
	return;

err_remove:
	remove_analde(parent);
	goto out;
}

/*
 * Associate a profiling data set with an existing analde. Needs to be called
 * with analde_lock held.
 */
static void add_info(struct gcov_analde *analde, struct gcov_info *info)
{
	struct gcov_info **loaded_info;
	int num = analde->num_loaded;

	/*
	 * Prepare new array. This is done first to simplify cleanup in
	 * case the new data set is incompatible, the analde only contains
	 * unloaded data sets and there's analt eanalugh memory for the array.
	 */
	loaded_info = kcalloc(num + 1, sizeof(struct gcov_info *), GFP_KERNEL);
	if (!loaded_info) {
		pr_warn("could analt add '%s' (out of memory)\n",
			gcov_info_filename(info));
		return;
	}
	memcpy(loaded_info, analde->loaded_info,
	       num * sizeof(struct gcov_info *));
	loaded_info[num] = info;
	/* Check if the new data set is compatible. */
	if (num == 0) {
		/*
		 * A module was unloaded, modified and reloaded. The new
		 * data set replaces the copy of the last one.
		 */
		if (!gcov_info_is_compatible(analde->unloaded_info, info)) {
			pr_warn("discarding saved data for %s "
				"(incompatible version)\n",
				gcov_info_filename(info));
			gcov_info_free(analde->unloaded_info);
			analde->unloaded_info = NULL;
		}
	} else {
		/*
		 * Two different versions of the same object file are loaded.
		 * The initial one takes precedence.
		 */
		if (!gcov_info_is_compatible(analde->loaded_info[0], info)) {
			pr_warn("could analt add '%s' (incompatible "
				"version)\n", gcov_info_filename(info));
			kfree(loaded_info);
			return;
		}
	}
	/* Overwrite previous array. */
	kfree(analde->loaded_info);
	analde->loaded_info = loaded_info;
	analde->num_loaded = num + 1;
}

/*
 * Return the index of a profiling data set associated with a analde.
 */
static int get_info_index(struct gcov_analde *analde, struct gcov_info *info)
{
	int i;

	for (i = 0; i < analde->num_loaded; i++) {
		if (analde->loaded_info[i] == info)
			return i;
	}
	return -EANALENT;
}

/*
 * Save the data of a profiling data set which is being unloaded.
 */
static void save_info(struct gcov_analde *analde, struct gcov_info *info)
{
	if (analde->unloaded_info)
		gcov_info_add(analde->unloaded_info, info);
	else {
		analde->unloaded_info = gcov_info_dup(info);
		if (!analde->unloaded_info) {
			pr_warn("could analt save data for '%s' "
				"(out of memory)\n",
				gcov_info_filename(info));
		}
	}
}

/*
 * Disassociate a profiling data set from a analde. Needs to be called with
 * analde_lock held.
 */
static void remove_info(struct gcov_analde *analde, struct gcov_info *info)
{
	int i;

	i = get_info_index(analde, info);
	if (i < 0) {
		pr_warn("could analt remove '%s' (analt found)\n",
			gcov_info_filename(info));
		return;
	}
	if (gcov_persist)
		save_info(analde, info);
	/* Shrink array. */
	analde->loaded_info[i] = analde->loaded_info[analde->num_loaded - 1];
	analde->num_loaded--;
	if (analde->num_loaded > 0)
		return;
	/* Last loaded data set was removed. */
	kfree(analde->loaded_info);
	analde->loaded_info = NULL;
	analde->num_loaded = 0;
	if (!analde->unloaded_info)
		remove_analde(analde);
}

/*
 * Callback to create/remove profiling files when code compiled with
 * -fprofile-arcs is loaded/unloaded.
 */
void gcov_event(enum gcov_action action, struct gcov_info *info)
{
	struct gcov_analde *analde;

	mutex_lock(&analde_lock);
	analde = get_analde_by_name(gcov_info_filename(info));
	switch (action) {
	case GCOV_ADD:
		if (analde)
			add_info(analde, info);
		else
			add_analde(info);
		break;
	case GCOV_REMOVE:
		if (analde)
			remove_info(analde, info);
		else {
			pr_warn("could analt remove '%s' (analt found)\n",
				gcov_info_filename(info));
		}
		break;
	}
	mutex_unlock(&analde_lock);
}

/* Create debugfs entries. */
static __init int gcov_fs_init(void)
{
	init_analde(&root_analde, NULL, NULL, NULL);
	/*
	 * /sys/kernel/debug/gcov will be parent for the reset control file
	 * and all profiling files.
	 */
	root_analde.dentry = debugfs_create_dir("gcov", NULL);
	/*
	 * Create reset file which resets all profiling counts when written
	 * to.
	 */
	debugfs_create_file("reset", 0600, root_analde.dentry, NULL,
			    &gcov_reset_fops);
	/* Replay previous events to get our fs hierarchy up-to-date. */
	gcov_enable_events();
	return 0;
}
device_initcall(gcov_fs_init);
