/*
 * Copyright (C) 2016 Noralf Trønnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_TINYDRM_H
#define __LINUX_TINYDRM_H

#include <drm/drm_simple_kms_helper.h>

struct drm_driver;

/**
 * struct tinydrm_device - tinydrm device
 */
struct tinydrm_device {
	/**
	 * @drm: DRM device
	 */
	struct drm_device *drm;

	/**
	 * @pipe: Display pipe structure
	 */
	struct drm_simple_display_pipe pipe;
};

static inline struct tinydrm_device *
pipe_to_tinydrm(struct drm_simple_display_pipe *pipe)
{
	return container_of(pipe, struct tinydrm_device, pipe);
}

int devm_tinydrm_init(struct device *parent, struct tinydrm_device *tdev,
		      struct drm_driver *driver);
int devm_tinydrm_register(struct tinydrm_device *tdev);

#endif /* __LINUX_TINYDRM_H */
