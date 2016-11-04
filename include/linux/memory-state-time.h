/* include/linux/memory-state-time.h
 *
 * Copyright (C) 2016 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/workqueue.h>

#define UPDATE_MEMORY_STATE(BLOCK, VALUE) BLOCK->update_call(BLOCK, VALUE)

struct memory_state_update_block;

typedef void (*memory_state_update_fn_t)(struct memory_state_update_block *ub,
		int value);

/* This struct is populated when you pass it to a memory_state_register*
 * function. The update_call function is used for an update and defined in the
 * typedef memory_state_update_fn_t
 */
struct memory_state_update_block {
	memory_state_update_fn_t update_call;
	int id;
};

/* Register a frequency struct memory_state_update_block to provide updates to
 * memory_state_time about frequency changes using its update_call function.
 */
struct memory_state_update_block *memory_state_register_frequency_source(void);

/* Register a bandwidth struct memory_state_update_block to provide updates to
 * memory_state_time about bandwidth changes using its update_call function.
 */
struct memory_state_update_block *memory_state_register_bandwidth_source(void);
