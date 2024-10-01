/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PATH_H
#define _LINUX_PATH_H

struct dentry;
struct vfsmount;

struct path {
	struct vfsmount *mnt;
	struct dentry *dentry;
} __randomize_layout;

extern void path_get(const struct path *);
extern void path_put(const struct path *);

static inline int path_equal(const struct path *path1, const struct path *path2)
{
	return path1->mnt == path2->mnt && path1->dentry == path2->dentry;
}

/*
 * Cleanup macro for use with __free(path_put). Avoids dereference and
 * copying @path unlike DEFINE_FREE(). path_put() will handle the empty
 * path correctly just ensure @path is initialized:
 *
 * struct path path __free(path_put) = {};
 */
#define __free_path_put path_put

#endif  /* _LINUX_PATH_H */
