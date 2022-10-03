/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/fsnotify_backend.h>
#include <linux/inotify.h>
#include <linux/slab.h> /* struct kmem_cache */

struct inotify_event_info {
	struct fsnotify_event fse;
	u32 mask;
	int wd;
	u32 sync_cookie;
	int name_len;
	char name[];
};

struct inotify_inode_mark {
	struct fsnotify_mark fsn_mark;
	int wd;
};

static inline struct inotify_event_info *INOTIFY_E(struct fsnotify_event *fse)
{
	return container_of(fse, struct inotify_event_info, fse);
}

/*
 * INOTIFY_USER_FLAGS represents all of the mask bits that we expose to
 * userspace.  There is at least one bit (FS_EVENT_ON_CHILD) which is
 * used only internally to the kernel.
 */
#define INOTIFY_USER_MASK (IN_ALL_EVENTS)

static inline __u32 inotify_mark_user_mask(struct fsnotify_mark *fsn_mark)
{
	__u32 mask = fsn_mark->mask & INOTIFY_USER_MASK;

	if (fsn_mark->flags & FSNOTIFY_MARK_FLAG_EXCL_UNLINK)
		mask |= IN_EXCL_UNLINK;
	if (fsn_mark->flags & FSNOTIFY_MARK_FLAG_IN_ONESHOT)
		mask |= IN_ONESHOT;

	return mask;
}

extern void inotify_ignored_and_remove_idr(struct fsnotify_mark *fsn_mark,
					   struct fsnotify_group *group);
extern int inotify_handle_inode_event(struct fsnotify_mark *inode_mark,
				      u32 mask, struct inode *inode,
				      struct inode *dir,
				      const struct qstr *name, u32 cookie);

extern const struct fsnotify_ops inotify_fsnotify_ops;
extern struct kmem_cache *inotify_inode_mark_cachep;

#ifdef CONFIG_INOTIFY_USER
static inline void dec_inotify_instances(struct ucounts *ucounts)
{
	dec_ucount(ucounts, UCOUNT_INOTIFY_INSTANCES);
}

static inline struct ucounts *inc_inotify_watches(struct ucounts *ucounts)
{
	return inc_ucount(ucounts->ns, ucounts->uid, UCOUNT_INOTIFY_WATCHES);
}

static inline void dec_inotify_watches(struct ucounts *ucounts)
{
	dec_ucount(ucounts, UCOUNT_INOTIFY_WATCHES);
}
#endif
