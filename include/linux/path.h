#ifndef _LINUX_PATH_H
#define _LINUX_PATH_H

struct dentry;
struct vfsmount;

struct path {
	struct vfsmount *mnt;
	struct dentry *dentry;
};

extern void path_get(struct path *);
extern void path_put(struct path *);

#endif  /* _LINUX_PATH_H */
