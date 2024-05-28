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

#include <linux/types.h>
#include <linux/seq_file.h>

#include <drm/drm_gpuvm.h>

/**
 * DRM_DEBUGFS_GPUVA_INFO - &drm_info_list entry to dump a GPU VA space
 * @show: the &drm_info_list's show callback
 * @data: driver private data
 *
 * Drivers should use this macro to define a &drm_info_list entry to provide a
 * debugfs file for dumping the GPU VA space regions and mappings.
 *
 * For each DRM GPU VA space drivers should call drm_debugfs_gpuva_info() from
 * their @show callback.
 */
#define DRM_DEBUGFS_GPUVA_INFO(show, data) {"gpuvas", show, DRIVER_GEM_GPUVA, data}

/**
 * struct drm_info_list - debugfs info list entry
 *
 * This structure represents a debugfs file to be created by the drm
 * core.
 */
struct drm_info_list {
	/** @name: file name */
	const char *name;
	/**
	 * @show:
	 *
	 * Show callback. &seq_file->private will be set to the &struct
	 * drm_info_node corresponding to the instance of this info on a given
	 * &struct drm_minor.
	 */
	int (*show)(struct seq_file*, void*);
	/** @driver_features: Required driver features for this entry */
	u32 driver_features;
	/** @data: Driver-private data, should not be device-specific. */
	void *data;
};

/**
 * struct drm_info_node - Per-minor debugfs node structure
 *
 * This structure represents a debugfs file, as an instantiation of a &struct
 * drm_info_list on a &struct drm_minor.
 *
 * FIXME:
 *
 * No it doesn't make a hole lot of sense that we duplicate debugfs entries for
 * both the render and the primary nodes, but that's how this has organically
 * grown. It should probably be fixed, with a compatibility link, if needed.
 */
struct drm_info_node {
	/** @minor: &struct drm_minor for this node. */
	struct drm_minor *minor;
	/** @info_ent: template for this node. */
	const struct drm_info_list *info_ent;
	/* private: */
	struct list_head list;
	struct dentry *dent;
};

/**
 * struct drm_debugfs_info - debugfs info list entry
 *
 * This structure represents a debugfs file to be created by the drm
 * core.
 */
struct drm_debugfs_info {
	/** @name: File name */
	const char *name;

	/**
	 * @show:
	 *
	 * Show callback. &seq_file->private will be set to the &struct
	 * drm_debugfs_entry corresponding to the instance of this info
	 * on a given &struct drm_device.
	 */
	int (*show)(struct seq_file*, void*);

	/** @driver_features: Required driver features for this entry. */
	u32 driver_features;

	/** @data: Driver-private data, should not be device-specific. */
	void *data;
};

/**
 * struct drm_debugfs_entry - Per-device debugfs node structure
 *
 * This structure represents a debugfs file, as an instantiation of a &struct
 * drm_debugfs_info on a &struct drm_device.
 */
struct drm_debugfs_entry {
	/** @dev: &struct drm_device for this node. */
	struct drm_device *dev;

	/** @file: Template for this node. */
	struct drm_debugfs_info file;

	/** @list: Linked list of all device nodes. */
	struct list_head list;
};

#if defined(CONFIG_DEBUG_FS)
void drm_debugfs_create_files(const struct drm_info_list *files,
			      int count, struct dentry *root,
			      struct drm_minor *minor);
int drm_debugfs_remove_files(const struct drm_info_list *files, int count,
			     struct dentry *root, struct drm_minor *minor);

void drm_debugfs_add_file(struct drm_device *dev, const char *name,
			  int (*show)(struct seq_file*, void*), void *data);

void drm_debugfs_add_files(struct drm_device *dev,
			   const struct drm_debugfs_info *files, int count);

int drm_debugfs_gpuva_info(struct seq_file *m,
			   struct drm_gpuvm *gpuvm);
#else
static inline void drm_debugfs_create_files(const struct drm_info_list *files,
					    int count, struct dentry *root,
					    struct drm_minor *minor)
{}

static inline int drm_debugfs_remove_files(const struct drm_info_list *files,
					   int count, struct dentry *root,
					   struct drm_minor *minor)
{
	return 0;
}

static inline void drm_debugfs_add_file(struct drm_device *dev, const char *name,
					int (*show)(struct seq_file*, void*),
					void *data)
{}

static inline void drm_debugfs_add_files(struct drm_device *dev,
					 const struct drm_debugfs_info *files,
					 int count)
{}

static inline int drm_debugfs_gpuva_info(struct seq_file *m,
					 struct drm_gpuvm *gpuvm)
{
	return 0;
}
#endif

#endif /* _DRM_DEBUGFS_H_ */
