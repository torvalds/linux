/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * posix-clock.h - support for dynamic clock devices
 *
 * Copyright (C) 2010 OMICRON electronics GmbH
 */
#ifndef _LINUX_POSIX_CLOCK_H_
#define _LINUX_POSIX_CLOCK_H_

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/posix-timers.h>
#include <linux/rwsem.h>

struct posix_clock;
struct posix_clock_context;

/**
 * struct posix_clock_operations - functional interface to the clock
 *
 * Every posix clock is represented by a character device. Drivers may
 * optionally offer extended capabilities by implementing the
 * character device methods. The character device file operations are
 * first handled by the clock device layer, then passed on to the
 * driver by calling these functions.
 *
 * @owner:          The clock driver should set to THIS_MODULE
 * @clock_adjtime:  Adjust the clock
 * @clock_gettime:  Read the current time
 * @clock_getres:   Get the clock resolution
 * @clock_settime:  Set the current time value
 * @open:           Optional character device open method
 * @release:        Optional character device release method
 * @ioctl:          Optional character device ioctl method
 * @read:           Optional character device read method
 * @poll:           Optional character device poll method
 */
struct posix_clock_operations {
	struct module *owner;

	int  (*clock_adjtime)(struct posix_clock *pc, struct __kernel_timex *tx);

	int  (*clock_gettime)(struct posix_clock *pc, struct timespec64 *ts);

	int  (*clock_getres) (struct posix_clock *pc, struct timespec64 *ts);

	int  (*clock_settime)(struct posix_clock *pc,
			      const struct timespec64 *ts);

	/*
	 * Optional character device methods:
	 */
	long (*ioctl)(struct posix_clock_context *pccontext, unsigned int cmd,
		      unsigned long arg);

	int (*open)(struct posix_clock_context *pccontext, fmode_t f_mode);

	__poll_t (*poll)(struct posix_clock_context *pccontext, struct file *file,
			 poll_table *wait);

	int (*release)(struct posix_clock_context *pccontext);

	ssize_t (*read)(struct posix_clock_context *pccontext, uint flags,
			char __user *buf, size_t cnt);
};

/**
 * struct posix_clock - represents a dynamic posix clock
 *
 * @ops:     Functional interface to the clock
 * @cdev:    Character device instance for this clock
 * @dev:     Pointer to the clock's device.
 * @rwsem:   Protects the 'zombie' field from concurrent access.
 * @zombie:  If 'zombie' is true, then the hardware has disappeared.
 *
 * Drivers should embed their struct posix_clock within a private
 * structure, obtaining a reference to it during callbacks using
 * container_of().
 *
 * Drivers should supply an initialized but not exposed struct device
 * to posix_clock_register(). It is used to manage lifetime of the
 * driver's private structure. It's 'release' field should be set to
 * a release function for this private structure.
 */
struct posix_clock {
	struct posix_clock_operations ops;
	struct cdev cdev;
	struct device *dev;
	struct rw_semaphore rwsem;
	bool zombie;
};

/**
 * struct posix_clock_context - represents clock file operations context
 *
 * @clk:              Pointer to the clock
 * @private_clkdata:  Pointer to user data
 *
 * Drivers should use struct posix_clock_context during specific character
 * device file operation methods to access the posix clock.
 *
 * Drivers can store a private data structure during the open operation
 * if they have specific information that is required in other file
 * operations.
 */
struct posix_clock_context {
	struct posix_clock *clk;
	void *private_clkdata;
};

/**
 * posix_clock_register() - register a new clock
 * @clk:   Pointer to the clock. Caller must provide 'ops' field
 * @dev:   Pointer to the initialized device. Caller must provide
 *         'release' field
 *
 * A clock driver calls this function to register itself with the
 * clock device subsystem. If 'clk' points to dynamically allocated
 * memory, then the caller must provide a 'release' function to free
 * that memory.
 *
 * Returns zero on success, non-zero otherwise.
 */
int posix_clock_register(struct posix_clock *clk, struct device *dev);

/**
 * posix_clock_unregister() - unregister a clock
 * @clk: Clock instance previously registered via posix_clock_register()
 *
 * A clock driver calls this function to remove itself from the clock
 * device subsystem. The posix_clock itself will remain (in an
 * inactive state) until its reference count drops to zero, at which
 * point it will be deallocated with its 'release' method.
 */
void posix_clock_unregister(struct posix_clock *clk);

#endif
