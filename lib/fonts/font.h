/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LIB_FONTS_FONT_H
#define _LIB_FONTS_FONT_H

#include <linux/font.h>

/*
 * Font data
 */

#define FONT_EXTRA_WORDS 4

struct font_data {
	unsigned int extra[FONT_EXTRA_WORDS];
	unsigned char data[];
} __packed;

/*
 * Built-in fonts
 */

#define VGA8x8_IDX	0
#define VGA8x16_IDX	1
#define PEARL8x8_IDX	2
#define VGA6x11_IDX	3
#define FONT7x14_IDX	4
#define	FONT10x18_IDX	5
#define SUN8x16_IDX	6
#define SUN12x22_IDX	7
#define ACORN8x8_IDX	8
#define	MINI4x6_IDX	9
#define FONT6x10_IDX	10
#define TER16x32_IDX	11
#define FONT6x8_IDX	12
#define TER10x18_IDX	13

#endif
