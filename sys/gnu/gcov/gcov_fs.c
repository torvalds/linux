// SPDX-License-Identifier: GPL-2.0
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301, USA.
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


#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/param.h>
#include <sys/sbuf.h>

#include <sys/queue.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/syslog.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>
#include <linux/debugfs.h>

#include <gnu/gcov/gcov.h>
#include <sys/queue.h>

extern int gcov_events_enabled;
static int gcov_persist;
static struct mtx gcov_mtx;
MTX_SYSINIT(gcov_init, &gcov_mtx, "gcov_mtx", MTX_DEF);
MALLOC_DEFINE(M_GCOV, "gcov", "gcov");

void __gcov_init(struct gcov_info *info);
void __gcov_flush(void);
void __gcov_merge_add(gcov_type *counters, unsigned int n_counters);
void __gcov_merge_single(gcov_type *counters, unsigned int n_counters);
void __gcov_merge_delta(gcov_type *counters, unsigned int n_counters);
void __gcov_merge_ior(gcov_type *counters, unsigned int n_counters);
void __gcov_merge_time_profile(gcov_type *counters, unsigned int n_counters);
void __gcov_merge_icall_topn(gcov_type *counters, unsigned int n_counters);
void __gcov_exit(void);

static void gcov_event(enum gcov_action action, struct gcov_info *info);


/*
 * Private copy taken from libc
 */
static char *
(basename)(char *path)
{
	char *ptr;

	/*
	 * If path is a null pointer or points to an empty string,
	 * basename() shall return a pointer to the string ".".
	 */
	if (path == NULL || *path == '\0')
		return (__DECONST(char *, "."));

	/* Find end of last pathname component and null terminate it. */
	ptr = path + strlen(path);
	while (ptr > path + 1 && *(ptr - 1) == '/')
		--ptr;
	*ptr-- = '\0';

	/* Find beginning of last pathname component. */
	while (ptr > path && *(ptr - 1) != '/')
		--ptr;
	return (ptr);
}

/*
 * __gcov_init is called by gcc-generated constructor code for each object
 * file compiled with -fprofile-arcs.
 */
void
__gcov_init(struct gcov_info *info)
{
	static unsigned int gcov_version;

	mtx_lock(&gcov_mtx);
	if (gcov_version == 0) {
		gcov_version = gcov_info_version(info);
		/*
		 * Printing gcc's version magic may prove useful for debugging
		 * incompatibility reports.
		 */
		log(LOG_INFO, "version magic: 0x%x\n", gcov_version);
	}
	/*
	 * Add new profiling data structure to list and inform event
	 * listener.
	 */
	gcov_info_link(info);
	if (gcov_events_enabled)
		gcov_event(GCOV_ADD, info);
	mtx_unlock(&gcov_mtx);
}

/*
 * These functions may be referenced by gcc-generated profiling code but serve
 * no function for kernel profiling.
 */
void
__gcov_flush(void)
{
	/* Unused. */
}

void
__gcov_merge_add(gcov_type *counters, unsigned int n_counters)
{
	/* Unused. */
}

void
__gcov_merge_single(gcov_type *counters, unsigned int n_counters)
{
	/* Unused. */
}

void
__gcov_merge_delta(gcov_type *counters, unsigned int n_counters)
{
	/* Unused. */
}

void
__gcov_merge_ior(gcov_type *counters, unsigned int n_counters)
{
	/* Unused. */
}

void
__gcov_merge_time_profile(gcov_type *counters, unsigned int n_counters)
{
	/* Unused. */
}

void
__gcov_merge_icall_topn(gcov_type *counters, unsigned int n_counters)
{
	/* Unused. */
}

void
__gcov_exit(void)
{
	/* Unused. */
}


/**
 * struct gcov_node - represents a debugfs entry
 * @entry: list entry for parent's child node list
 * @children: child nodes
 * @all_entry: list entry for list of all nodes
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
	LIST_ENTRY(gcov_node) children_entry;
	LIST_ENTRY(gcov_node) all_entry;
	struct {
		struct gcov_node *lh_first;
	} children;
	struct gcov_node *parent;
	struct gcov_info **loaded_info;
	struct gcov_info *unloaded_info;
	struct dentry *dentry;
	struct dentry **links;
	int num_loaded;
	char name[0];
};

#ifdef notyet
static const char objtree[] = OBJTREE;
static const char srctree[] = SRCTREE;
#else
static const char objtree[] = "";
static const char srctree[] = "";
#endif
static struct gcov_node root_node;
static struct {
	struct gcov_node *lh_first;
} all_head;
static struct mtx node_lock;
MTX_SYSINIT(node_init, &node_lock, "node_lock", MTX_DEF);
static void remove_node(struct gcov_node *node);

/*
 * seq_file.start() implementation for gcov data files. Note that the
 * gcov_iterator interface is designed to be more restrictive than seq_file
 * (no start from arbitrary position, etc.), to simplify the iterator
 * implementation.
 */
static void *
gcov_seq_start(struct seq_file *seq, off_t *pos)
{
	off_t i;

	gcov_iter_start(seq->private);
	for (i = 0; i < *pos; i++) {
		if (gcov_iter_next(seq->private))
			return NULL;
	}
	return seq->private;
}

/* seq_file.next() implementation for gcov data files. */
static void *
gcov_seq_next(struct seq_file *seq, void *data, off_t *pos)
{
	struct gcov_iterator *iter = data;

	if (gcov_iter_next(iter))
		return NULL;
	(*pos)++;

	return iter;
}

/* seq_file.show() implementation for gcov data files. */
static int
gcov_seq_show(struct seq_file *seq, void *data)
{
	struct gcov_iterator *iter = data;

	if (gcov_iter_write(iter, seq->buf))
		return (-EINVAL);
	return (0);
}

static void
gcov_seq_stop(struct seq_file *seq, void *data)
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
static struct gcov_info *
get_node_info(struct gcov_node *node)
{
	if (node->num_loaded > 0)
		return (node->loaded_info[0]);

	return (node->unloaded_info);
}

/*
 * Return a newly allocated profiling data set which contains the sum of
 * all profiling data associated with the given node.
 */
static struct gcov_info *
get_accumulated_info(struct gcov_node *node)
{
	struct gcov_info *info;
	int i = 0;

	if (node->unloaded_info)
		info = gcov_info_dup(node->unloaded_info);
	else
		info = gcov_info_dup(node->loaded_info[i++]);
	if (info == NULL)
		return (NULL);
	for (; i < node->num_loaded; i++)
		gcov_info_add(info, node->loaded_info[i]);

	return (info);
}

/*
 * open() implementation for gcov data files. Create a copy of the profiling
 * data set and initialize the iterator and seq_file interface.
 */
static int
gcov_seq_open(struct inode *inode, struct file *file)
{
	struct gcov_node *node = inode->i_private;
	struct gcov_iterator *iter;
	struct seq_file *seq;
	struct gcov_info *info;
	int rc = -ENOMEM;

	mtx_lock(&node_lock);
	/*
	 * Read from a profiling data copy to minimize reference tracking
	 * complexity and concurrent access and to keep accumulating multiple
	 * profiling data sets associated with one node simple.
	 */
	info = get_accumulated_info(node);
	if (info == NULL)
		goto out_unlock;
	iter = gcov_iter_new(info);
	if (iter == NULL)
		goto err_free_info;
	rc = seq_open(file, &gcov_seq_ops);
	if (rc)
		goto err_free_iter_info;
	seq = file->private_data;
	seq->private = iter;
out_unlock:
	mtx_unlock(&node_lock);
	return (rc);

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
static int
gcov_seq_release(struct inode *inode, struct file *file)
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

	return (0);
}

/*
 * Find a node by the associated data file name. Needs to be called with
 * node_lock held.
 */
static struct gcov_node *
get_node_by_name(const char *name)
{
	struct gcov_node *node;
	struct gcov_info *info;

	LIST_FOREACH(node, &all_head, all_entry) {
		info = get_node_info(node);
		if (info && (strcmp(gcov_info_filename(info), name) == 0))
			return (node);
	}

	return (NULL);
}

/*
 * Reset all profiling data associated with the specified node.
 */
static void
reset_node(struct gcov_node *node)
{
	int i;

	if (node->unloaded_info)
		gcov_info_reset(node->unloaded_info);
	for (i = 0; i < node->num_loaded; i++)
		gcov_info_reset(node->loaded_info[i]);
}

void
gcov_stats_reset(void)
{
	struct gcov_node *node;

	mtx_lock(&node_lock);
 restart:
	LIST_FOREACH(node, &all_head, all_entry) {
		if (node->num_loaded > 0)
			reset_node(node);
		else if (LIST_EMPTY(&node->children)) {
			remove_node(node);
			goto restart;
		}
	}
	mtx_unlock(&node_lock);
}

/*
 * write() implementation for gcov data files. Reset profiling data for the
 * corresponding file. If all associated object files have been unloaded,
 * remove the debug fs node as well.
 */
static ssize_t
gcov_seq_write(struct file *file, const char *addr, size_t len, off_t *pos)
{
	struct seq_file *seq;
	struct gcov_info *info;
	struct gcov_node *node;

	seq = file->private_data;
	info = gcov_iter_get_info(seq->private);
	mtx_lock(&node_lock);
	node = get_node_by_name(gcov_info_filename(info));
	if (node) {
		/* Reset counts or remove node for unloaded modules. */
		if (node->num_loaded == 0)
			remove_node(node);
		else
			reset_node(node);
	}
	/* Reset counts for open file. */
	gcov_info_reset(info);
	mtx_unlock(&node_lock);

	return (len);
}

/*
 * Given a string <path> representing a file path of format:
 *   path/to/file.gcda
 * construct and return a new string:
 *   <dir/>path/to/file.<ext>
 */
static char *
link_target(const char *dir, const char *path, const char *ext)
{
	char *target;
	char *old_ext;
	char *copy;

	copy = strdup_flags(path, M_GCOV, M_NOWAIT);
	if (!copy)
		return (NULL);
	old_ext = strrchr(copy, '.');
	if (old_ext)
		*old_ext = '\0';
	target = NULL;
	if (dir)
		asprintf(&target, M_GCOV, "%s/%s.%s", dir, copy, ext);
	else
		asprintf(&target, M_GCOV, "%s.%s", copy, ext);
	free(copy, M_GCOV);

	return (target);
}

/*
 * Construct a string representing the symbolic link target for the given
 * gcov data file name and link type. Depending on the link type and the
 * location of the data file, the link target can either point to a
 * subdirectory of srctree, objtree or in an external location.
 */
static char *
get_link_target(const char *filename, const struct gcov_link *ext)
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

	return (result);
}

#define SKEW_PREFIX	".tmp_"

/*
 * For a filename .tmp_filename.ext return filename.ext. Needed to compensate
 * for filename skewing caused by the mod-versioning mechanism.
 */
static const char *
deskew(const char *basename)
{
	if (strncmp(basename, SKEW_PREFIX, sizeof(SKEW_PREFIX) - 1) == 0)
		return (basename + sizeof(SKEW_PREFIX) - 1);
	return (basename);
}

/*
 * Create links to additional files (usually .c and .gcno files) which the
 * gcov tool expects to find in the same directory as the gcov data file.
 */
static void
add_links(struct gcov_node *node, struct dentry *parent)
{
	const char *path_basename;
	char *target;
	int num;
	int i;

	for (num = 0; gcov_link[num].ext; num++)
		/* Nothing. */;
	node->links = malloc((num*sizeof(struct dentry *)), M_GCOV, M_NOWAIT|M_ZERO);
	if (node->links == NULL)
		return;
	for (i = 0; i < num; i++) {
		target = get_link_target(
				gcov_info_filename(get_node_info(node)),
				&gcov_link[i]);
		if (target == NULL)
			goto out_err;
		path_basename = basename(target);
		if (path_basename == target)
			goto out_err;
		node->links[i] = debugfs_create_symlink(deskew(path_basename),
							parent,	target);
		if (!node->links[i])
			goto out_err;
		free(target, M_GCOV);
	}

	return;
out_err:
	free(target, M_GCOV);
	while (i-- > 0)
		debugfs_remove(node->links[i]);
	free(node->links, M_GCOV);
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
static void
init_node(struct gcov_node *node, struct gcov_info *info,
   const char *name, struct gcov_node *parent)
{
	LIST_INIT(&node->children);
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
static struct gcov_node *
new_node(struct gcov_node *parent, struct gcov_info *info, const char *name)
{
	struct gcov_node *node;

	node = malloc(sizeof(struct gcov_node) + strlen(name) + 1, M_GCOV, M_NOWAIT|M_ZERO);
	if (!node)
		goto err_nomem;
	if (info) {
		node->loaded_info = malloc(sizeof(struct gcov_info *), M_GCOV, M_NOWAIT|M_ZERO);
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
		log(LOG_WARNING, "could not create file\n");
		free(node, M_GCOV);
		return NULL;
	}
	if (info)
		add_links(node, parent->dentry);
	LIST_INSERT_HEAD(&parent->children, node, children_entry);
	LIST_INSERT_HEAD(&all_head, node, all_entry);

	return (node);

err_nomem:
	free(node, M_GCOV);
	log(LOG_WARNING, "out of memory\n");
	return NULL;
}

/* Remove symbolic links associated with node. */
static void
remove_links(struct gcov_node *node)
{

	if (node->links == NULL)
		return;
	for (int i = 0; gcov_link[i].ext; i++)
		debugfs_remove(node->links[i]);
	free(node->links, M_GCOV);
	node->links = NULL;
}

/*
 * Remove node from all lists and debugfs and release associated resources.
 * Needs to be called with node_lock held.
 */
static void
release_node(struct gcov_node *node)
{
	LIST_REMOVE(node, children_entry);
	LIST_REMOVE(node, all_entry);
	debugfs_remove(node->dentry);
	remove_links(node);
	free(node->loaded_info, M_GCOV);
	if (node->unloaded_info)
		gcov_info_free(node->unloaded_info);
	free(node, M_GCOV);
}

/* Release node and empty parents. Needs to be called with node_lock held. */
static void
remove_node(struct gcov_node *node)
{
	struct gcov_node *parent;

	while ((node != &root_node) && LIST_EMPTY(&node->children)) {
		parent = node->parent;
		release_node(node);
		node = parent;
	}
}

/*
 * Find child node with given basename. Needs to be called with node_lock
 * held.
 */
static struct gcov_node *
get_child_by_name(struct gcov_node *parent, const char *name)
{
	struct gcov_node *node;

	LIST_FOREACH(node, &parent->children, children_entry) {
		if (strcmp(node->name, name) == 0)
			return (node);
	}

	return (NULL);
}

/*
 * Create a node for a given profiling data set and add it to all lists and
 * debugfs. Needs to be called with node_lock held.
 */
static void
add_node(struct gcov_info *info)
{
	char *filename;
	char *curr;
	char *next;
	struct gcov_node *parent;
	struct gcov_node *node;

	filename = strdup_flags(gcov_info_filename(info), M_GCOV, M_NOWAIT);
	if (filename == NULL)
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
	free(filename, M_GCOV);
	return;

err_remove:
	remove_node(parent);
	goto out;
}

/*
 * Associate a profiling data set with an existing node. Needs to be called
 * with node_lock held.
 */
static void
add_info(struct gcov_node *node, struct gcov_info *info)
{
	struct gcov_info **loaded_info;
	int num = node->num_loaded;

	/*
	 * Prepare new array. This is done first to simplify cleanup in
	 * case the new data set is incompatible, the node only contains
	 * unloaded data sets and there's not enough memory for the array.
	 */
	loaded_info = malloc((num + 1)* sizeof(struct gcov_info *), M_GCOV, M_NOWAIT|M_ZERO);
	if (!loaded_info) {
		log(LOG_WARNING, "could not add '%s' (out of memory)\n",
			gcov_info_filename(info));
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
			log(LOG_WARNING, "discarding saved data for %s "
				"(incompatible version)\n",
				gcov_info_filename(info));
			gcov_info_free(node->unloaded_info);
			node->unloaded_info = NULL;
		}
	} else {
		/*
		 * Two different versions of the same object file are loaded.
		 * The initial one takes precedence.
		 */
		if (!gcov_info_is_compatible(node->loaded_info[0], info)) {
			log(LOG_WARNING, "could not add '%s' (incompatible "
				"version)\n", gcov_info_filename(info));
			free(loaded_info, M_GCOV);
			return;
		}
	}
	/* Overwrite previous array. */
	free(node->loaded_info, M_GCOV);
	node->loaded_info = loaded_info;
	node->num_loaded = num + 1;
}

/*
 * Return the index of a profiling data set associated with a node.
 */
static int
get_info_index(struct gcov_node *node, struct gcov_info *info)
{
	int i;

	for (i = 0; i < node->num_loaded; i++) {
		if (node->loaded_info[i] == info)
			return (i);
	}
	return (ENOENT);
}

/*
 * Save the data of a profiling data set which is being unloaded.
 */
static void
save_info(struct gcov_node *node, struct gcov_info *info)
{
	if (node->unloaded_info)
		gcov_info_add(node->unloaded_info, info);
	else {
		node->unloaded_info = gcov_info_dup(info);
		if (!node->unloaded_info) {
			log(LOG_WARNING, "could not save data for '%s' "
				"(out of memory)\n",
				gcov_info_filename(info));
		}
	}
}

/*
 * Disassociate a profiling data set from a node. Needs to be called with
 * node_lock held.
 */
static void
remove_info(struct gcov_node *node, struct gcov_info *info)
{
	int i;

	i = get_info_index(node, info);
	if (i < 0) {
		log(LOG_WARNING, "could not remove '%s' (not found)\n",
			gcov_info_filename(info));
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
	free(node->loaded_info, M_GCOV);
	node->loaded_info = NULL;
	node->num_loaded = 0;
	if (!node->unloaded_info)
		remove_node(node);
}

/*
 * Callback to create/remove profiling files when code compiled with
 * -fprofile-arcs is loaded/unloaded.
 */
static void
gcov_event(enum gcov_action action, struct gcov_info *info)
{
	struct gcov_node *node;

	mtx_lock(&node_lock);
	node = get_node_by_name(gcov_info_filename(info));
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
			log(LOG_WARNING, "could not remove '%s' (not found)\n",
				gcov_info_filename(info));
		}
		break;
	}
	mtx_unlock(&node_lock);
}

/**
 * gcov_enable_events - enable event reporting through gcov_event()
 *
 * Turn on reporting of profiling data load/unload-events through the
 * gcov_event() callback. Also replay all previous events once. This function
 * is needed because some events are potentially generated too early for the
 * callback implementation to handle them initially.
 */
void
gcov_enable_events(void)
{
	struct gcov_info *info = NULL;
	int count;

	mtx_lock(&gcov_mtx);
	count = 0;

	/* Perform event callback for previously registered entries. */
	while ((info = gcov_info_next(info))) {
		gcov_event(GCOV_ADD, info);
		sched_relinquish(curthread);
		count++;
	}

	mtx_unlock(&gcov_mtx);
	printf("%s found %d events\n", __func__, count);
}

/* Update list and generate events when modules are unloaded. */
void
gcov_module_unload(void *arg __unused, module_t mod)
{
	struct gcov_info *info = NULL;
	struct gcov_info *prev = NULL;

	mtx_lock(&gcov_mtx );

	/* Remove entries located in module from linked list. */
	while ((info = gcov_info_next(info))) {
		if (within_module((vm_offset_t)info, mod)) {
			gcov_info_unlink(prev, info);
			if (gcov_events_enabled)
				gcov_event(GCOV_REMOVE, info);
		} else
			prev = info;
	}

	mtx_unlock(&gcov_mtx);
}

void
gcov_fs_init(void)
{
	init_node(&root_node, NULL, NULL, NULL);
	root_node.dentry = debugfs_create_dir("gcov", NULL);
}
