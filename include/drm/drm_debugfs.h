/*
 * Internal Header for the Direct Rendering Manager
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * Copyright (c) 2009-2010, Code Aurora Forum.
 * All rights reserved.
 *
 * Author: Rickard E. (Rik) Faith <faith@valinux.com>
 * Author: Gareth Hughes <gareth@valinux.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _DRM_DEBUGFS_H_
#define _DRM_DEBUGFS_H_

/**
 * Info file list entry. This structure represents a debugfs or proc file to
 * be created by the drm core
 */
struct drm_info_list {
	const char *name; /** file name */
	int (*show)(struct seq_file*, void*); /** show callback */
	u32 driver_features; /**< Required driver features for this entry */
	void *data;
};

/**
 * debugfs node structure. This structure represents a debugfs file.
 */
struct drm_info_node {
	struct list_head list;
	struct drm_minor *minor;
	const struct drm_info_list *info_ent;
	struct dentry *dent;
};

#if defined(CONFIG_DEBUG_FS)
int drm_debugfs_create_files(const struct drm_info_list *files,
			     int count, struct dentry *root,
			     struct drm_minor *minor);
int drm_debugfs_remove_files(const struct drm_info_list *files,
			     int count, struct drm_minor *minor);
#else
static inline int drm_debugfs_create_files(const struct drm_info_list *files,
					   int count, struct dentry *root,
					   struct drm_minor *minor)
{
	return 0;
}

static inline int drm_debugfs_remove_files(const struct drm_info_list *files,
					   int count, struct drm_minor *minor)
{
	return 0;
}
#endif

#endif /* _DRM_DEBUGFS_H_ */
