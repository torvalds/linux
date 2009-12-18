#include <linux/fanotify.h>
#include <linux/fsnotify_backend.h>
#include <linux/net.h>
#include <linux/kernel.h>
#include <linux/types.h>

extern const struct fsnotify_ops fanotify_fsnotify_ops;

static inline bool fanotify_mask_valid(__u32 mask)
{
	if (mask & ~((__u32)FAN_ALL_INCOMING_EVENTS))
		return false;
	return true;
}
