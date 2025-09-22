/*
 * Copyright(c) 2011-2015 Intel Corporation. All rights reserved.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _I915_VGPU_H_
#define _I915_VGPU_H_

#include <linux/types.h>

struct drm_i915_private;
struct i915_ggtt;

void intel_vgpu_detect(struct drm_i915_private *i915);
bool intel_vgpu_active(struct drm_i915_private *i915);
void intel_vgpu_register(struct drm_i915_private *i915);
bool intel_vgpu_has_full_ppgtt(struct drm_i915_private *i915);
bool intel_vgpu_has_hwsp_emulation(struct drm_i915_private *i915);
bool intel_vgpu_has_huge_gtt(struct drm_i915_private *i915);

int intel_vgt_balloon(struct i915_ggtt *ggtt);
void intel_vgt_deballoon(struct i915_ggtt *ggtt);

#endif /* _I915_VGPU_H_ */
