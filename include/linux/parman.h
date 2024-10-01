/*
 * include/linux/parman.h - Manager for linear priority array areas
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Jiri Pirko <jiri@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _PARMAN_H
#define _PARMAN_H

#include <linux/list.h>

enum parman_algo_type {
	PARMAN_ALGO_TYPE_LSORT,
};

struct parman_item {
	struct list_head list;
	unsigned long index;
};

struct parman_prio {
	struct list_head list;
	struct list_head item_list;
	unsigned long priority;
};

struct parman_ops {
	unsigned long base_count;
	unsigned long resize_step;
	int (*resize)(void *priv, unsigned long new_count);
	void (*move)(void *priv, unsigned long from_index,
		     unsigned long to_index, unsigned long count);
	enum parman_algo_type algo;
};

struct parman;

struct parman *parman_create(const struct parman_ops *ops, void *priv);
void parman_destroy(struct parman *parman);
void parman_prio_init(struct parman *parman, struct parman_prio *prio,
		      unsigned long priority);
void parman_prio_fini(struct parman_prio *prio);
int parman_item_add(struct parman *parman, struct parman_prio *prio,
		    struct parman_item *item);
void parman_item_remove(struct parman *parman, struct parman_prio *prio,
			struct parman_item *item);

#endif
