/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Apple Silicon hardware tunable support
 *
 * Each tunable is a list with each entry containing a offset into the MMIO
 * region, a mask of bits to be cleared and a set of bits to be set. These
 * tunables are passed along by the previous boot stages and vary from device
 * to device such that they cannot be hardcoded in the individual drivers.
 *
 * Copyright (C) The Asahi Linux Contributors
 */

#ifndef _LINUX_SOC_APPLE_TUNABLE_H_
#define _LINUX_SOC_APPLE_TUNABLE_H_

#include <linux/device.h>
#include <linux/types.h>

/**
 * Struct to store an Apple Silicon hardware tunable.
 *
 * Each tunable is a list with each entry containing a offset into the MMIO
 * region, a mask of bits to be cleared and a set of bits to be set. These
 * tunables are passed along by the previous boot stages and vary from device
 * to device such that they cannot be hardcoded in the individual drivers.
 *
 * @param sz Number of [offset, mask, value] tuples stored in values.
 * @param values [offset, mask, value] array.
 */
struct apple_tunable {
	size_t sz;
	struct {
		u32 offset;
		u32 mask;
		u32 value;
	} values[] __counted_by(sz);
};

/**
 * Parse an array of hardware tunables from the device tree.
 *
 * @dev: Device node used for devm_kzalloc internally.
 * @np: Device node which contains the tunable array.
 * @name: Name of the device tree property which contains the tunables.
 * @res: Resource to which the tunables will be applied, used for bound checking
 *
 * @return: devres allocated struct on success or PTR_ERR on failure.
 */
struct apple_tunable *devm_apple_tunable_parse(struct device *dev,
					       struct device_node *np,
					       const char *name,
					       struct resource *res);

/**
 * Apply a previously loaded hardware tunable.
 *
 * @param regs: MMIO to which the tunable will be applied.
 * @param tunable: Pointer to the tunable.
 */
void apple_tunable_apply(void __iomem *regs, struct apple_tunable *tunable);

#endif
