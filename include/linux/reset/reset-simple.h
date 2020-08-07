/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Simple Reset Controller ops
 *
 * Based on Allwinner SoCs Reset Controller driver
 *
 * Copyright 2013 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#ifndef __RESET_SIMPLE_H__
#define __RESET_SIMPLE_H__

#include <linux/io.h>
#include <linux/reset-controller.h>
#include <linux/spinlock.h>

/**
 * struct reset_simple_data - driver data for simple reset controllers
 * @lock: spinlock to protect registers during read-modify-write cycles
 * @membase: memory mapped I/O register range
 * @rcdev: reset controller device base structure
 * @active_low: if true, bits are cleared to assert the reset. Otherwise, bits
 *              are set to assert the reset. Note that this says nothing about
 *              the voltage level of the actual reset line.
 * @status_active_low: if true, bits read back as cleared while the reset is
 *                     asserted. Otherwise, bits read back as set while the
 *                     reset is asserted.
 * @reset_us: Minimum delay in microseconds needed that needs to be
 *            waited for between an assert and a deassert to reset the
 *            device. If multiple consumers with different delay
 *            requirements are connected to this controller, it must
 *            be the largest minimum delay. 0 means that such a delay is
 *            unknown and the reset operation is unsupported.
 */
struct reset_simple_data {
	spinlock_t			lock;
	void __iomem			*membase;
	struct reset_controller_dev	rcdev;
	bool				active_low;
	bool				status_active_low;
	unsigned int			reset_us;
};

extern const struct reset_control_ops reset_simple_ops;

#endif /* __RESET_SIMPLE_H__ */
