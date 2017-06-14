/* Copyright (c) 2017 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#ifndef __MAP_IN_MAP_H__
#define __MAP_IN_MAP_H__

#include <linux/types.h>

struct file;
struct bpf_map;

struct bpf_map *bpf_map_meta_alloc(int inner_map_ufd);
void bpf_map_meta_free(struct bpf_map *map_meta);
bool bpf_map_meta_equal(const struct bpf_map *meta0,
			const struct bpf_map *meta1);
void *bpf_map_fd_get_ptr(struct bpf_map *map, struct file *map_file,
			 int ufd);
void bpf_map_fd_put_ptr(void *ptr);

#endif
