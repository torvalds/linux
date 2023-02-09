/* SPDX-License-Identifier: GPL-2.0 */

#ifndef VIDEO_CMDLINE_H
#define VIDEO_CMDLINE_H

#include <linux/types.h>

#if defined(CONFIG_VIDEO_CMDLINE)
const char *video_get_options(const char *name);

/* exported for compatibility with fbdev; don't use in new code */
bool __video_get_options(const char *name, const char **option, bool is_of);
#else
static inline const char *video_get_options(const char *name)
{
	return NULL;
}
#endif

#endif
