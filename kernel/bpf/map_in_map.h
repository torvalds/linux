/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2017 Facebook
 */
#ifndef __MAP_IN_MAP_H__
#define __MAP_IN_MAP_H__

#include <linux/types.h>

struct file;
struct bpf_map;

struct bpf_map *bpf_map_meta_alloc(int inner_map_ufd);
void bpf_map_meta_free(struct bpf_map *map_meta);
void *bpf_map_fd_get_ptr(struct bpf_map *map, struct file *map_file,
			 int ufd);
void bpf_map_fd_put_ptr(struct bpf_map *map, void *ptr, bool need_defer);
u32 bpf_map_fd_sys_lookup_elem(void *ptr);

#endif
