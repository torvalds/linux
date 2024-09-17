/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 BayLibre, SAS
 */
#ifndef __SOC_MESON_CANVAS_H
#define __SOC_MESON_CANVAS_H

#include <linux/kernel.h>

#define MESON_CANVAS_WRAP_NONE	0x00
#define MESON_CANVAS_WRAP_X	0x01
#define MESON_CANVAS_WRAP_Y	0x02

#define MESON_CANVAS_BLKMODE_LINEAR	0x00
#define MESON_CANVAS_BLKMODE_32x32	0x01
#define MESON_CANVAS_BLKMODE_64x64	0x02

#define MESON_CANVAS_ENDIAN_SWAP16	0x1
#define MESON_CANVAS_ENDIAN_SWAP32	0x3
#define MESON_CANVAS_ENDIAN_SWAP64	0x7
#define MESON_CANVAS_ENDIAN_SWAP128	0xf

struct device;
struct meson_canvas;

/**
 * meson_canvas_get() - get a canvas provider instance
 *
 * @dev: consumer device pointer
 */
struct meson_canvas *meson_canvas_get(struct device *dev);

/**
 * meson_canvas_alloc() - take ownership of a canvas
 *
 * @canvas: canvas provider instance retrieved from meson_canvas_get()
 * @canvas_index: will be filled with the canvas ID
 */
int meson_canvas_alloc(struct meson_canvas *canvas, u8 *canvas_index);

/**
 * meson_canvas_free() - remove ownership from a canvas
 *
 * @canvas: canvas provider instance retrieved from meson_canvas_get()
 * @canvas_index: canvas ID that was obtained via meson_canvas_alloc()
 */
int meson_canvas_free(struct meson_canvas *canvas, u8 canvas_index);

/**
 * meson_canvas_config() - configure a canvas
 *
 * @canvas: canvas provider instance retrieved from meson_canvas_get()
 * @canvas_index: canvas ID that was obtained via meson_canvas_alloc()
 * @addr: physical address to the pixel buffer
 * @stride: width of the buffer
 * @height: height of the buffer
 * @wrap: undocumented
 * @blkmode: block mode (linear, 32x32, 64x64)
 * @endian: byte swapping (swap16, swap32, swap64, swap128)
 */
int meson_canvas_config(struct meson_canvas *canvas, u8 canvas_index,
			u32 addr, u32 stride, u32 height,
			unsigned int wrap, unsigned int blkmode,
			unsigned int endian);

#endif
