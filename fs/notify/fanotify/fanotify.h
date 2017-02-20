#include <linux/fsnotify_backend.h>
#include <linux/path.h>
#include <linux/slab.h>

extern struct kmem_cache *fanotify_event_cachep;
extern struct kmem_cache *fanotify_perm_event_cachep;

/*
 * Structure for normal fanotify events. It gets allocated in
 * fanotify_handle_event() and freed when the information is retrieved by
 * userspace
 */
struct fanotify_event_info {
	struct fsnotify_event fse;
	/*
	 * We hold ref to this path so it may be dereferenced at any point
	 * during this object's lifetime
	 */
	struct path path;
	struct pid *tgid;
};

#ifdef CONFIG_FANOTIFY_ACCESS_PERMISSIONS
/*
 * Structure for permission fanotify events. It gets allocated and freed in
 * fanotify_handle_event() since we wait there for user response. When the
 * information is retrieved by userspace the structure is moved from
 * group->notification_list to group->fanotify_data.access_list to wait for
 * user response.
 */
struct fanotify_perm_event_info {
	struct fanotify_event_info fae;
	int response;	/* userspace answer to question */
	int fd;		/* fd we passed to userspace for this event */
};

static inline struct fanotify_perm_event_info *
FANOTIFY_PE(struct fsnotify_event *fse)
{
	return container_of(fse, struct fanotify_perm_event_info, fae.fse);
}
#endif

static inline struct fanotify_event_info *FANOTIFY_E(struct fsnotify_event *fse)
{
	return container_of(fse, struct fanotify_event_info, fse);
}

struct fanotify_event_info *fanotify_alloc_event(struct inode *inode, u32 mask,
						 const struct path *path);
