#ifndef _LINUX_NAMEI_H
#define _LINUX_NAMEI_H

#include <linux/kernel.h>
#include <linux/path.h>
#include <linux/fcntl.h>
#include <linux/errno.h>

enum { MAX_NESTED_LINKS = 8 };

#define MAXSYMLINKS 40

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
#define LOOKUP_AUTOMOUNT	0x0004

#define LOOKUP_PARENT		0x0010
#define LOOKUP_REVAL		0x0020
#define LOOKUP_RCU		0x0040

/*
 * Intent data
 */
#define LOOKUP_OPEN		0x0100
#define LOOKUP_CREATE		0x0200
#define LOOKUP_EXCL		0x0400
#define LOOKUP_RENAME_TARGET	0x0800

#define LOOKUP_JUMPED		0x1000
#define LOOKUP_ROOT		0x2000
#define LOOKUP_EMPTY		0x4000
#define LOOKUP_CASE_INSENSITIVE 0x8000

extern int user_path_at_empty(int, const char __user *, unsigned, struct path *, int *empty);

static inline int user_path_at(int dfd, const char __user *name, unsigned flags,
		 struct path *path)
{
	return user_path_at_empty(dfd, name, flags, path, NULL);
}

static inline int user_path(const char __user *name, struct path *path)
{
	return user_path_at_empty(AT_FDCWD, name, LOOKUP_FOLLOW, path, NULL);
}

static inline int user_lpath(const char __user *name, struct path *path)
{
	return user_path_at_empty(AT_FDCWD, name, 0, path, NULL);
}

static inline int user_path_dir(const char __user *name, struct path *path)
{
	return user_path_at_empty(AT_FDCWD, name,
				  LOOKUP_FOLLOW | LOOKUP_DIRECTORY, path, NULL);
}

extern int kern_path(const char *, unsigned, struct path *);

extern struct dentry *kern_path_create(int, const char *, struct path *, unsigned int);
extern struct dentry *user_path_create(int, const char __user *, struct path *, unsigned int);
extern void done_path_create(struct path *, struct dentry *);
extern struct dentry *kern_path_locked(const char *, struct path *);
extern int kern_path_mountpoint(int, const char *, struct path *, unsigned int);
extern int vfs_path_lookup(struct dentry *, struct vfsmount *,
		const char *, unsigned int, struct path *);

extern struct dentry *lookup_one_len(const char *, struct dentry *, int);

extern int follow_down_one(struct path *);
extern int follow_down(struct path *);
extern int follow_up(struct path *);

extern struct dentry *lock_rename(struct dentry *, struct dentry *);
extern void unlock_rename(struct dentry *, struct dentry *);

extern void nd_jump_link(struct path *path);

static inline void nd_terminate_link(void *name, size_t len, size_t maxlen)
{
	((char *) name)[min(len, maxlen)] = '\0';
}

/**
 * retry_estale - determine whether the caller should retry an operation
 * @error: the error that would currently be returned
 * @flags: flags being used for next lookup attempt
 *
 * Check to see if the error code was -ESTALE, and then determine whether
 * to retry the call based on whether "flags" already has LOOKUP_REVAL set.
 *
 * Returns true if the caller should try the operation again.
 */
static inline bool
retry_estale(const long error, const unsigned int flags)
{
	return error == -ESTALE && !(flags & LOOKUP_REVAL);
}

#endif /* _LINUX_NAMEI_H */
