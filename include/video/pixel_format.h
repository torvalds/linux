/* SPDX-License-Identifier: GPL-2.0 */

#ifndef VIDEO_PIXEL_FORMAT_H
#define VIDEO_PIXEL_FORMAT_H

struct pixel_format {
	unsigned char bits_per_pixel;
	bool indexed;
	union {
		struct {
			struct {
				unsigned char offset;
				unsigned char length;
			} alpha, red, green, blue;
		};
		struct {
			unsigned char offset;
			unsigned char length;
		} index;
	};
};

#define PIXEL_FORMAT_XRGB1555 \
	{ 16, false, { .alpha = {0, 0}, .red = {10, 5}, .green = {5, 5}, .blue = {0, 5} } }

#define PIXEL_FORMAT_RGB565 \
	{ 16, false, { .alpha = {0, 0}, .red = {11, 5}, .green = {5, 6}, .blue = {0, 5} } }

#define PIXEL_FORMAT_RGB888 \
	{ 24, false, { .alpha = {0, 0}, .red = {16, 8}, .green = {8, 8}, .blue = {0, 8} } }

#define PIXEL_FORMAT_XRGB8888 \
	{ 32, false, { .alpha = {0, 0}, .red = {16, 8}, .green = {8, 8}, .blue = {0, 8} } }

#define PIXEL_FORMAT_XBGR8888 \
	{ 32, false, { .alpha = {0, 0}, .red = {0, 8}, .green = {8, 8}, .blue = {16, 8} } }

#define PIXEL_FORMAT_XRGB2101010 \
	{ 32, false, { .alpha = {0, 0}, .red = {20, 10}, .green = {10, 10}, .blue = {0, 10} } }

#endif
