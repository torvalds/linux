#ifndef __FS_NOTIFY_FSNOTIFY_H_
#define __FS_NOTIFY_FSNOTIFY_H_

#include <linux/list.h>
#include <linux/fsnotify.h>
#include <linux/srcu.h>
#include <linux/types.h>

/* protects reads of fsnotify_groups */
extern struct srcu_struct fsnotify_grp_srcu;
/* all groups which receive fsnotify events */
extern struct list_head fsnotify_groups;
/* all bitwise OR of all event types (FS_*) for all fsnotify_groups */
extern __u32 fsnotify_mask;
#endif	/* __FS_NOTIFY_FSNOTIFY_H_ */
