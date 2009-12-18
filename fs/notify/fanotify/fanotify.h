#include <linux/fanotify.h>
#include <linux/fsnotify_backend.h>
#include <linux/net.h>
#include <linux/kernel.h>
#include <linux/types.h>

extern const struct fsnotify_ops fanotify_fsnotify_ops;

static inline bool fanotify_mark_flags_valid(unsigned int flags)
{
	/* must be either and add or a remove */
	if (!(flags & (FAN_MARK_ADD | FAN_MARK_REMOVE)))
		return false;

	/* cannot be both add and remove */
	if ((flags & FAN_MARK_ADD) &&
	    (flags & FAN_MARK_REMOVE))
		return false;

	/* cannot have more flags than we know about */
	if (flags & ~FAN_ALL_MARK_FLAGS)
		return false;

	return true;
}

static inline bool fanotify_mask_valid(__u32 mask)
{
	if (mask & ~((__u32)FAN_ALL_INCOMING_EVENTS))
		return false;
	return true;
}

static inline __u32 fanotify_outgoing_mask(__u32 mask)
{
	return mask & FAN_ALL_OUTGOING_EVENTS;
}
