/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/fsyestify_backend.h>
#include <linux/iyestify.h>
#include <linux/slab.h> /* struct kmem_cache */

struct iyestify_event_info {
	struct fsyestify_event fse;
	u32 mask;
	int wd;
	u32 sync_cookie;
	int name_len;
	char name[];
};

struct iyestify_iyesde_mark {
	struct fsyestify_mark fsn_mark;
	int wd;
};

static inline struct iyestify_event_info *INOTIFY_E(struct fsyestify_event *fse)
{
	return container_of(fse, struct iyestify_event_info, fse);
}

extern void iyestify_igyesred_and_remove_idr(struct fsyestify_mark *fsn_mark,
					   struct fsyestify_group *group);
extern int iyestify_handle_event(struct fsyestify_group *group,
				struct iyesde *iyesde,
				u32 mask, const void *data, int data_type,
				const struct qstr *file_name, u32 cookie,
				struct fsyestify_iter_info *iter_info);

extern const struct fsyestify_ops iyestify_fsyestify_ops;
extern struct kmem_cache *iyestify_iyesde_mark_cachep;

#ifdef CONFIG_INOTIFY_USER
static inline void dec_iyestify_instances(struct ucounts *ucounts)
{
	dec_ucount(ucounts, UCOUNT_INOTIFY_INSTANCES);
}

static inline struct ucounts *inc_iyestify_watches(struct ucounts *ucounts)
{
	return inc_ucount(ucounts->ns, ucounts->uid, UCOUNT_INOTIFY_WATCHES);
}

static inline void dec_iyestify_watches(struct ucounts *ucounts)
{
	dec_ucount(ucounts, UCOUNT_INOTIFY_WATCHES);
}
#endif
