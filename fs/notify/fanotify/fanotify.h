#include <linux/fanotify.h>
#include <linux/fsnotify_backend.h>
#include <linux/net.h>
#include <linux/kernel.h>
#include <linux/types.h>

extern const struct fsnotify_ops fanotify_fsnotify_ops;

static inline __u32 fanotify_outgoing_mask(__u32 mask)
{
	return mask & FAN_ALL_OUTGOING_EVENTS;
}
