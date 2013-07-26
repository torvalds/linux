/*
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#ifndef _I915_DRM_H_
#define _I915_DRM_H_

#include <drm/i915_pciids.h>
#include <uapi/drm/i915_drm.h>

/* For use by IPS driver */
extern unsigned long i915_read_mch_val(void);
extern bool i915_gpu_raise(void);
extern bool i915_gpu_lower(void);
extern bool i915_gpu_busy(void);
extern bool i915_gpu_turbo_disable(void);

/*
 * The Bridge device's PCI config space has information about the
 * fb aperture size and the amount of pre-reserved memory.
 * This is all handled in the intel-gtt.ko module. i915.ko only
 * cares about the vga bit for the vga rbiter.
 */
#define INTEL_GMCH_CTRL		0x52
#define INTEL_GMCH_VGA_DISABLE  (1 << 1)
#define SNB_GMCH_CTRL		0x50
#define    SNB_GMCH_GGMS_SHIFT	8 /* GTT Graphics Memory Size */
#define    SNB_GMCH_GGMS_MASK	0x3
#define    SNB_GMCH_GMS_SHIFT   3 /* Graphics Mode Select */
#define    SNB_GMCH_GMS_MASK    0x1f

#define I830_GMCH_CTRL			0x52

#define I855_GMCH_GMS_MASK		0xF0
#define I855_GMCH_GMS_STOLEN_0M		0x0
#define I855_GMCH_GMS_STOLEN_1M		(0x1 << 4)
#define I855_GMCH_GMS_STOLEN_4M		(0x2 << 4)
#define I855_GMCH_GMS_STOLEN_8M		(0x3 << 4)
#define I855_GMCH_GMS_STOLEN_16M	(0x4 << 4)
#define I855_GMCH_GMS_STOLEN_32M	(0x5 << 4)
#define I915_GMCH_GMS_STOLEN_48M	(0x6 << 4)
#define I915_GMCH_GMS_STOLEN_64M	(0x7 << 4)
#define G33_GMCH_GMS_STOLEN_128M	(0x8 << 4)
#define G33_GMCH_GMS_STOLEN_256M	(0x9 << 4)
#define INTEL_GMCH_GMS_STOLEN_96M	(0xa << 4)
#define INTEL_GMCH_GMS_STOLEN_160M	(0xb << 4)
#define INTEL_GMCH_GMS_STOLEN_224M	(0xc << 4)
#define INTEL_GMCH_GMS_STOLEN_352M	(0xd << 4)

#endif				/* _I915_DRM_H_ */
