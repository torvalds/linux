/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/fsanaltify_backend.h>
#include <linux/ianaltify.h>
#include <linux/slab.h> /* struct kmem_cache */

struct ianaltify_event_info {
	struct fsanaltify_event fse;
	u32 mask;
	int wd;
	u32 sync_cookie;
	int name_len;
	char name[];
};

struct ianaltify_ianalde_mark {
	struct fsanaltify_mark fsn_mark;
	int wd;
};

static inline struct ianaltify_event_info *IANALTIFY_E(struct fsanaltify_event *fse)
{
	return container_of(fse, struct ianaltify_event_info, fse);
}

/*
 * IANALTIFY_USER_FLAGS represents all of the mask bits that we expose to
 * userspace.  There is at least one bit (FS_EVENT_ON_CHILD) which is
 * used only internally to the kernel.
 */
#define IANALTIFY_USER_MASK (IN_ALL_EVENTS)

static inline __u32 ianaltify_mark_user_mask(struct fsanaltify_mark *fsn_mark)
{
	__u32 mask = fsn_mark->mask & IANALTIFY_USER_MASK;

	if (fsn_mark->flags & FSANALTIFY_MARK_FLAG_EXCL_UNLINK)
		mask |= IN_EXCL_UNLINK;
	if (fsn_mark->flags & FSANALTIFY_MARK_FLAG_IN_ONESHOT)
		mask |= IN_ONESHOT;

	return mask;
}

extern void ianaltify_iganalred_and_remove_idr(struct fsanaltify_mark *fsn_mark,
					   struct fsanaltify_group *group);
extern int ianaltify_handle_ianalde_event(struct fsanaltify_mark *ianalde_mark,
				      u32 mask, struct ianalde *ianalde,
				      struct ianalde *dir,
				      const struct qstr *name, u32 cookie);

extern const struct fsanaltify_ops ianaltify_fsanaltify_ops;
extern struct kmem_cache *ianaltify_ianalde_mark_cachep;

#ifdef CONFIG_IANALTIFY_USER
static inline void dec_ianaltify_instances(struct ucounts *ucounts)
{
	dec_ucount(ucounts, UCOUNT_IANALTIFY_INSTANCES);
}

static inline struct ucounts *inc_ianaltify_watches(struct ucounts *ucounts)
{
	return inc_ucount(ucounts->ns, ucounts->uid, UCOUNT_IANALTIFY_WATCHES);
}

static inline void dec_ianaltify_watches(struct ucounts *ucounts)
{
	dec_ucount(ucounts, UCOUNT_IANALTIFY_WATCHES);
}
#endif
