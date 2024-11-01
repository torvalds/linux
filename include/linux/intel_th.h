/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel(R) Trace Hub data structures for implementing buffer sinks.
 *
 * Copyright (C) 2019 Intel Corporation.
 */

#ifndef _INTEL_TH_H_
#define _INTEL_TH_H_

#include <linux/scatterlist.h>

/* MSC operating modes (MSC_MODE) */
enum {
	MSC_MODE_SINGLE	= 0,
	MSC_MODE_MULTI,
	MSC_MODE_EXI,
	MSC_MODE_DEBUG,
};

struct msu_buffer {
	const char	*name;
	/*
	 * ->assign() called when buffer 'mode' is set to this driver
	 *   (aka mode_store())
	 * @device:	struct device * of the msc
	 * @mode:	allows the driver to set HW mode (see the enum above)
	 * Returns:	a pointer to a private structure associated with this
	 *		msc or NULL in case of error. This private structure
	 *		will then be passed into all other callbacks.
	 */
	void	*(*assign)(struct device *dev, int *mode);
	/* ->unassign():	some other mode is selected, clean up */
	void	(*unassign)(void *priv);
	/*
	 * ->alloc_window(): allocate memory for the window of a given
	 *		size
	 * @sgt:	pointer to sg_table, can be overridden by the buffer
	 *		driver, or kept intact
	 * Returns:	number of sg table entries <= number of pages;
	 *		0 is treated as an allocation failure.
	 */
	int	(*alloc_window)(void *priv, struct sg_table **sgt,
				size_t size);
	void	(*free_window)(void *priv, struct sg_table *sgt);
	/* ->activate():	trace has started */
	void	(*activate)(void *priv);
	/* ->deactivate():	trace is about to stop */
	void	(*deactivate)(void *priv);
	/*
	 * ->ready():	window @sgt is filled up to the last block OR
	 *		tracing is stopped by the user; this window contains
	 *		@bytes data. The window in question transitions into
	 *		the "LOCKED" state, indicating that it can't be used
	 *		by hardware. To clear this state and make the window
	 *		available to the hardware again, call
	 *		intel_th_msc_window_unlock().
	 */
	int	(*ready)(void *priv, struct sg_table *sgt, size_t bytes);
};

int intel_th_msu_buffer_register(const struct msu_buffer *mbuf,
				 struct module *owner);
void intel_th_msu_buffer_unregister(const struct msu_buffer *mbuf);
void intel_th_msc_window_unlock(struct device *dev, struct sg_table *sgt);

#define module_intel_th_msu_buffer(__buffer) \
static int __init __buffer##_init(void) \
{ \
	return intel_th_msu_buffer_register(&(__buffer), THIS_MODULE); \
} \
module_init(__buffer##_init); \
static void __exit __buffer##_exit(void) \
{ \
	intel_th_msu_buffer_unregister(&(__buffer)); \
} \
module_exit(__buffer##_exit);

#endif /* _INTEL_TH_H_ */
