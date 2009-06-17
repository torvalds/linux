#include <linux/fsnotify_backend.h>
#include <linux/inotify.h>
#include <linux/slab.h> /* struct kmem_cache */

extern struct kmem_cache *event_priv_cachep;

struct inotify_event_private_data {
	struct fsnotify_event_private_data fsnotify_event_priv_data;
	int wd;
};

struct inotify_inode_mark_entry {
	/* fsnotify_mark_entry MUST be the first thing */
	struct fsnotify_mark_entry fsn_entry;
	int wd;
};

extern void inotify_destroy_mark_entry(struct fsnotify_mark_entry *entry, struct fsnotify_group *group);
extern void inotify_free_event_priv(struct fsnotify_event_private_data *event_priv);

extern const struct fsnotify_ops inotify_fsnotify_ops;
