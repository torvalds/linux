#ifndef _LINUX_NAMEI_H
#define _LINUX_NAMEI_H

#include <linux/dcache.h>
#include <linux/linkage.h>
#include <linux/path.h>

struct vfsmount;

struct open_intent {
	int	flags;
	int	create_mode;
	struct file *file;
};

enum { MAX_NESTED_LINKS = 8 };

struct nameidata {
	struct path	path;
	struct qstr	last;
	struct path	root;
	struct file	*file;
	struct inode	*inode; /* path.dentry.d_inode */
	unsigned int	flags;
	unsigned	seq;
	int		last_type;
	unsigned	depth;
	char *saved_names[MAX_NESTED_LINKS + 1];

	/* Intent data */
	union {
		struct open_intent open;
	} intent;
};

/*
 * Type of the last component on LOOKUP_PARENT
 */
enum {LAST_NORM, LAST_ROOT, LAST_DOT, LAST_DOTDOT, LAST_BIND};

/*
 * The bitmask for a lookup event:
 *  - follow links at the end
 *  - require a directory
 *  - ending slashes ok even for nonexistent files
 *  - internal "there are more path components" flag
 *  - dentry cache is untrusted; force a real lookup
 *  - suppress terminal automount
 */
#define LOOKUP_FOLLOW		0x0001
#define LOOKUP_DIRECTORY	0x0002
#define LOOKUP_CONTINUE		0x0004

#define LOOKUP_PARENT		0x0010
#define LOOKUP_REVAL		0x0020
#define LOOKUP_RCU		0x0040
#define LOOKUP_NO_AUTOMOUNT	0x0080
/*
 * Intent data
 */
#define LOOKUP_OPEN		0x0100
#define LOOKUP_CREATE		0x0200
#define LOOKUP_EXCL		0x0400
#define LOOKUP_RENAME_TARGET	0x0800

extern int user_path_at(int, const char __user *, unsigned, struct path *);

#define user_path(name, path) user_path_at(AT_FDCWD, name, LOOKUP_FOLLOW, path)
#define user_lpath(name, path) user_path_at(AT_FDCWD, name, 0, path)
#define user_path_dir(name, path) \
	user_path_at(AT_FDCWD, name, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, path)

extern int kern_path(const char *, unsigned, struct path *);

extern int path_lookup(const char *, unsigned, struct nameidata *);
extern int vfs_path_lookup(struct dentry *, struct vfsmount *,
			   const char *, unsigned int, struct nameidata *);

extern struct file *lookup_instantiate_filp(struct nameidata *nd, struct dentry *dentry,
		int (*open)(struct inode *, struct file *));

extern struct dentry *lookup_one_len(const char *, struct dentry *, int);

extern int follow_down_one(struct path *);
extern int follow_down(struct path *, bool);
extern int follow_up(struct path *);

extern struct dentry *lock_rename(struct dentry *, struct dentry *);
extern void unlock_rename(struct dentry *, struct dentry *);

static inline void nd_set_link(struct nameidata *nd, char *path)
{
	nd->saved_names[nd->depth] = path;
}

static inline char *nd_get_link(struct nameidata *nd)
{
	return nd->saved_names[nd->depth];
}

static inline void nd_terminate_link(void *name, size_t len, size_t maxlen)
{
	((char *) name)[min(len, maxlen)] = '\0';
}

#endif /* _LINUX_NAMEI_H */
