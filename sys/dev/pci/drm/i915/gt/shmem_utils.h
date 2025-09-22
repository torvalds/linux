/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef SHMEM_UTILS_H
#define SHMEM_UTILS_H

#include <linux/types.h>

struct iosys_map;
struct drm_i915_gem_object;
#ifdef __linux__
struct file;

struct file *shmem_create_from_data(const char *name, void *data, size_t len);
struct file *shmem_create_from_object(struct drm_i915_gem_object *obj);

void *shmem_pin_map(struct file *file);
void shmem_unpin_map(struct file *file, void *ptr);

int shmem_read_to_iosys_map(struct file *file, loff_t off,
			    struct iosys_map *map, size_t map_off, size_t len);
int shmem_read(struct file *file, loff_t off, void *dst, size_t len);
int shmem_write(struct file *file, loff_t off, void *src, size_t len);
#endif /* __linux__ */

struct uvm_object *
uao_create_from_data(const char *, void *, size_t);
struct uvm_object *
uao_create_from_object(struct drm_i915_gem_object *);
int uao_read_to_iosys_map(struct uvm_object *, loff_t,
	struct iosys_map *, size_t, size_t);
int uao_read(struct uvm_object *, loff_t, void *, size_t);
int uao_write(struct uvm_object *, loff_t, void *, size_t);

#endif /* SHMEM_UTILS_H */
