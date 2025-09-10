/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Javier Carrasco <javier.carrasco@wolfvision.net>
 */

#ifndef _TOUCH_OVERLAY
#define _TOUCH_OVERLAY

#include <linux/types.h>

struct input_dev;

int touch_overlay_map(struct list_head *list, struct input_dev *input);

void touch_overlay_get_touchscreen_abs(struct list_head *list, u16 *x, u16 *y);

bool touch_overlay_mapped_touchscreen(struct list_head *list);

bool touch_overlay_process_contact(struct list_head *list,
				   struct input_dev *input,
				   struct input_mt_pos *pos, int slot);

void touch_overlay_sync_frame(struct list_head *list, struct input_dev *input);

#endif
