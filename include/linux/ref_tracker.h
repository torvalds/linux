// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _LINUX_REF_TRACKER_H
#define _LINUX_REF_TRACKER_H
#include <linux/refcount.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/stackdepot.h>

#define __ostream_printf __printf(2, 3)

struct ref_tracker;

struct ref_tracker_dir {
#ifdef CONFIG_REF_TRACKER
	spinlock_t		lock;
	unsigned int		quarantine_avail;
	refcount_t		untracked;
	refcount_t		no_tracker;
	bool			dead;
	struct list_head	list; /* List of active trackers */
	struct list_head	quarantine; /* List of dead trackers */
	const char		*class; /* object classname */
	char			name[32];
#endif
};

#ifdef CONFIG_REF_TRACKER

#ifdef CONFIG_DEBUG_FS

void ref_tracker_dir_debugfs(struct ref_tracker_dir *dir);

#else /* CONFIG_DEBUG_FS */

static inline void ref_tracker_dir_debugfs(struct ref_tracker_dir *dir)
{
}

#endif /* CONFIG_DEBUG_FS */

static inline void ref_tracker_dir_init(struct ref_tracker_dir *dir,
					unsigned int quarantine_count,
					const char *class,
					const char *name)
{
	INIT_LIST_HEAD(&dir->list);
	INIT_LIST_HEAD(&dir->quarantine);
	spin_lock_init(&dir->lock);
	dir->quarantine_avail = quarantine_count;
	dir->dead = false;
	refcount_set(&dir->untracked, 1);
	refcount_set(&dir->no_tracker, 1);
	dir->class = class;
	strscpy(dir->name, name, sizeof(dir->name));
	ref_tracker_dir_debugfs(dir);
	stack_depot_init();
}

void ref_tracker_dir_exit(struct ref_tracker_dir *dir);

void ref_tracker_dir_print_locked(struct ref_tracker_dir *dir,
				  unsigned int display_limit);

void ref_tracker_dir_print(struct ref_tracker_dir *dir,
			   unsigned int display_limit);

int ref_tracker_dir_snprint(struct ref_tracker_dir *dir, char *buf, size_t size);

int ref_tracker_alloc(struct ref_tracker_dir *dir,
		      struct ref_tracker **trackerp, gfp_t gfp);

int ref_tracker_free(struct ref_tracker_dir *dir,
		     struct ref_tracker **trackerp);

#else /* CONFIG_REF_TRACKER */

static inline void ref_tracker_dir_init(struct ref_tracker_dir *dir,
					unsigned int quarantine_count,
					const char *class,
					const char *name)
{
}

static inline void ref_tracker_dir_debugfs(struct ref_tracker_dir *dir)
{
}

static inline void ref_tracker_dir_exit(struct ref_tracker_dir *dir)
{
}

static inline void ref_tracker_dir_print_locked(struct ref_tracker_dir *dir,
						unsigned int display_limit)
{
}

static inline void ref_tracker_dir_print(struct ref_tracker_dir *dir,
					 unsigned int display_limit)
{
}

static inline int ref_tracker_dir_snprint(struct ref_tracker_dir *dir,
					  char *buf, size_t size)
{
	return 0;
}

static inline int ref_tracker_alloc(struct ref_tracker_dir *dir,
				    struct ref_tracker **trackerp,
				    gfp_t gfp)
{
	return 0;
}

static inline int ref_tracker_free(struct ref_tracker_dir *dir,
				   struct ref_tracker **trackerp)
{
	return 0;
}

#endif

#endif /* _LINUX_REF_TRACKER_H */
