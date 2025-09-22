/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_GEM_MMAN_H__
#define __I915_GEM_MMAN_H__

#include <linux/mm_types.h>
#include <linux/types.h>

struct drm_device;
struct drm_file;
struct drm_i915_gem_object;
struct drm_gem_object;
struct file;
struct i915_mmap_offset;
struct rwlock;

int i915_gem_mmap_gtt_version(void);
#ifdef __linux__
int i915_gem_mmap(struct file *filp, struct vm_area_struct *vma);
#else
struct uvm_object *i915_gem_mmap(struct file *filp, vm_prot_t accessprot,
				 voff_t off, vsize_t size);
#endif

int i915_gem_dumb_mmap_offset(struct drm_file *file_priv,
			      struct drm_device *dev,
			      u32 handle, u64 *offset);

void __i915_gem_object_release_mmap_gtt(struct drm_i915_gem_object *obj);
void i915_gem_object_release_mmap_gtt(struct drm_i915_gem_object *obj);

void i915_gem_object_runtime_pm_release_mmap_offset(struct drm_i915_gem_object *obj);
void i915_gem_object_release_mmap_offset(struct drm_i915_gem_object *obj);
#ifdef __linux__
int i915_gem_fb_mmap(struct drm_i915_gem_object *obj, struct vm_area_struct *vma);
#endif

int i915_gem_fault(struct drm_gem_object *gem_obj, struct uvm_faultinfo *ufi,
		   off_t offset, vaddr_t vaddr, vm_page_t *pps, int npages,
		   int centeridx, vm_prot_t access_type, int flags);

#endif
