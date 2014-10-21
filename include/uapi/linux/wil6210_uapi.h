/*
 * Copyright (c) 2014 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __WIL6210_UAPI_H__
#define __WIL6210_UAPI_H__

#if !defined(__KERNEL__)
#define __user
#endif

#include <linux/sockios.h>

/* Numbers SIOCDEVPRIVATE and SIOCDEVPRIVATE + 1
 * are used by Android devices to implement PNO (preferred network offload).
 * Albeit it is temporary solution, use different numbers to avoid conflicts
 */

/**
 * Perform 32-bit I/O operation to the card memory
 *
 * User code should arrange data in memory like this:
 *
 *	struct wil_memio io;
 *	struct ifreq ifr = {
 *		.ifr_data = &io,
 *	};
 */
#define WIL_IOCTL_MEMIO (SIOCDEVPRIVATE + 2)

/**
 * Perform block I/O operation to the card memory
 *
 * User code should arrange data in memory like this:
 *
 *	void *buf;
 *	struct wil_memio_block io = {
 *		.block = buf,
 *	};
 *	struct ifreq ifr = {
 *		.ifr_data = &io,
 *	};
 */
#define WIL_IOCTL_MEMIO_BLOCK (SIOCDEVPRIVATE + 3)

/**
 * operation to perform
 *
 * @wil_mmio_op_mask - bits defining operation,
 * @wil_mmio_addr_mask - bits defining addressing mode
 */
enum wil_memio_op {
	wil_mmio_read = 0,
	wil_mmio_write = 1,
	wil_mmio_op_mask = 0xff,
	wil_mmio_addr_linker = 0 << 8,
	wil_mmio_addr_ahb = 1 << 8,
	wil_mmio_addr_bar = 2 << 8,
	wil_mmio_addr_mask = 0xff00,
};

struct wil_memio {
	uint32_t op; /* enum wil_memio_op */
	uint32_t addr; /* should be 32-bit aligned */
	uint32_t val;
};

struct wil_memio_block {
	uint32_t op; /* enum wil_memio_op */
	uint32_t addr; /* should be 32-bit aligned */
	uint32_t size; /* should be multiple of 4 */
	void __user *block; /* block address */
};

#endif /* __WIL6210_UAPI_H__ */
