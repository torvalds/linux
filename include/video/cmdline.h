/* SPDX-License-Identifier: GPL-2.0 */

#ifndef VIDEO_CMDLINE_H
#define VIDEO_CMDLINE_H

#include <linux/kconfig.h>
#include <linux/types.h>

const char *video_get_options(const char *name);

#if IS_ENABLED(CONFIG_FB_CORE)
/* exported for compatibility with fbdev; don't use in new code */
bool __video_get_options(const char *name, const char **option, bool is_of);
#endif

#endif
