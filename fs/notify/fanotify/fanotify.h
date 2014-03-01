#include <linux/fsnotify_backend.h>
#include <linux/path.h>
#include <linux/slab.h>

extern struct kmem_cache *fanotify_event_cachep;

/*
 * Lifetime of the structure differs for normal and permission events. In both
 * cases the structure is allocated in fanotify_handle_event(). For normal
 * events the structure is freed immediately after reporting it to userspace.
 * For permission events we free it only after we receive response from
 * userspace.
 */
struct fanotify_event_info {
	struct fsnotify_event fse;
	/*
	 * We hold ref to this path so it may be dereferenced at any point
	 * during this object's lifetime
	 */
	struct path path;
	struct pid *tgid;
#ifdef CONFIG_FANOTIFY_ACCESS_PERMISSIONS
	u32 response;	/* userspace answer to question */
#endif
};

static inline struct fanotify_event_info *FANOTIFY_E(struct fsnotify_event *fse)
{
	return container_of(fse, struct fanotify_event_info, fse);
}
