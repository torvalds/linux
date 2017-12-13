/*
 * Copyright (c) 2014 Sebastian Reichel <sre@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef _TOUCHSCREEN_H
#define _TOUCHSCREEN_H

struct input_dev;
struct input_mt_pos;

struct touchscreen_properties {
	unsigned int min_x;
	unsigned int max_x;
	unsigned int min_y;
	unsigned int max_y;
	bool invert_x;
	bool invert_y;
	bool swap_x_y;
};

void touchscreen_parse_properties(struct input_dev *input, bool multitouch,
				  struct touchscreen_properties *prop);

void touchscreen_set_mt_pos(struct input_mt_pos *pos,
			    const struct touchscreen_properties *prop,
			    unsigned int x, unsigned int y);

void touchscreen_report_pos(struct input_dev *input,
			    const struct touchscreen_properties *prop,
			    unsigned int x, unsigned int y,
			    bool multitouch);

#endif
