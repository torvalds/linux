/*
 * Copyright © 2015 Intel Corporation
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Rafael Antognolli <rafael.antognolli@intel.com>
 *
 */

#ifndef DRM_DP_AUX_DEV
#define DRM_DP_AUX_DEV

#include <drm/drm_dp_helper.h>

#ifdef CONFIG_DRM_DP_AUX_CHARDEV

int drm_dp_aux_dev_init(void);
void drm_dp_aux_dev_exit(void);
int drm_dp_aux_register_devnode(struct drm_dp_aux *aux);
void drm_dp_aux_unregister_devnode(struct drm_dp_aux *aux);

#else

static inline int drm_dp_aux_dev_init(void)
{
	return 0;
}

static inline void drm_dp_aux_dev_exit(void)
{
}

static inline int drm_dp_aux_register_devnode(struct drm_dp_aux *aux)
{
	return 0;
}

static inline void drm_dp_aux_unregister_devnode(struct drm_dp_aux *aux)
{
}

#endif

#endif
