/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/fsnotify_backend.h>
#include <linux/inotify.h>
#include <linux/slab.h> /* struct kmem_cache */

struct inotify_event_info {
	struct fsnotify_event fse;
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

extern void inotify_ignored_and_remove_idr(struct fsnotify_mark *fsn_mark,
					   struct fsnotify_group *group);
extern int inotify_handle_event(struct fsnotify_group *group,
				struct inode *inode,
				u32 mask, const void *data, int data_type,
				const unsigned char *file_name, u32 cookie,
				struct fsnotify_iter_info *iter_info);

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
