/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2014 The Linux Foundation. All rights reserved.
 */

#ifndef _LINUX_IOPOLL_H
#define _LINUX_IOPOLL_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/io.h>

/**
 * poll_timeout_us - Periodically poll and perform an operation until
 *                   a condition is met or a timeout occurs
 *
 * @op: Operation
 * @cond: Break condition
 * @sleep_us: Maximum time to sleep between operations in us (0 tight-loops).
 *            Please read usleep_range() function description for details and
 *            limitations.
 * @timeout_us: Timeout in us, 0 means never timeout
 * @sleep_before_op: if it is true, sleep @sleep_us before operation.
 *
 * When available, you'll probably want to use one of the specialized
 * macros defined below rather than this macro directly.
 *
 * Returns: 0 on success and -ETIMEDOUT upon a timeout. Must not
 * be called from atomic context if sleep_us or timeout_us are used.
 */
#define poll_timeout_us(op, cond, sleep_us, timeout_us, sleep_before_op) \
({ \
	u64 __timeout_us = (timeout_us); \
	unsigned long __sleep_us = (sleep_us); \
	ktime_t __timeout = ktime_add_us(ktime_get(), __timeout_us); \
	int ___ret; \
	might_sleep_if((__sleep_us) != 0); \
	if ((sleep_before_op) && __sleep_us) \
		usleep_range((__sleep_us >> 2) + 1, __sleep_us); \
	for (;;) { \
		bool __expired = __timeout_us && \
			ktime_compare(ktime_get(), __timeout) > 0; \
		/* guarantee 'op' and 'cond' are evaluated after timeout expired */ \
		barrier(); \
		op; \
		if (cond) { \
			___ret = 0; \
			break; \
		} \
		if (__expired) { \
			___ret = -ETIMEDOUT; \
			break; \
		} \
		if (__sleep_us) \
			usleep_range((__sleep_us >> 2) + 1, __sleep_us); \
		cpu_relax(); \
	} \
	___ret; \
})

/**
 * poll_timeout_us_atomic - Periodically poll and perform an operation until
 *                          a condition is met or a timeout occurs
 *
 * @op: Operation
 * @cond: Break condition
 * @delay_us: Time to udelay between operations in us (0 tight-loops).
 *            Please read udelay() function description for details and
 *            limitations.
 * @timeout_us: Timeout in us, 0 means never timeout
 * @delay_before_op: if it is true, delay @delay_us before operation.
 *
 * This macro does not rely on timekeeping.  Hence it is safe to call even when
 * timekeeping is suspended, at the expense of an underestimation of wall clock
 * time, which is rather minimal with a non-zero delay_us.
 *
 * When available, you'll probably want to use one of the specialized
 * macros defined below rather than this macro directly.
 *
 * Returns: 0 on success and -ETIMEDOUT upon a timeout.
 */
#define poll_timeout_us_atomic(op, cond, delay_us, timeout_us, \
			       delay_before_op) \
({ \
	u64 __timeout_us = (timeout_us); \
	s64 __left_ns = __timeout_us * NSEC_PER_USEC; \
	unsigned long __delay_us = (delay_us); \
	u64 __delay_ns = __delay_us * NSEC_PER_USEC; \
	int ___ret; \
	if ((delay_before_op) && __delay_us) { \
		udelay(__delay_us); \
		if (__timeout_us) \
			__left_ns -= __delay_ns; \
	} \
	for (;;) { \
		bool __expired = __timeout_us && __left_ns < 0; \
		/* guarantee 'op' and 'cond' are evaluated after timeout expired */ \
		barrier(); \
		op; \
		if (cond) { \
			___ret = 0; \
			break; \
		} \
		if (__expired) { \
			___ret = -ETIMEDOUT; \
			break; \
		} \
		if (__delay_us) { \
			udelay(__delay_us); \
			if (__timeout_us) \
				__left_ns -= __delay_ns; \
		} \
		cpu_relax(); \
		if (__timeout_us) \
			__left_ns--; \
	} \
	___ret; \
})

/**
 * read_poll_timeout - Periodically poll an address until a condition is
 *                     met or a timeout occurs
 * @op: accessor function (takes @args as its arguments)
 * @val: Variable to read the value into
 * @cond: Break condition (usually involving @val)
 * @sleep_us: Maximum time to sleep between reads in us (0 tight-loops). Please
 *            read usleep_range() function description for details and
 *            limitations.
 * @timeout_us: Timeout in us, 0 means never timeout
 * @sleep_before_read: if it is true, sleep @sleep_us before read.
 * @args: arguments for @op poll
 *
 * When available, you'll probably want to use one of the specialized
 * macros defined below rather than this macro directly.
 *
 * Returns: 0 on success and -ETIMEDOUT upon a timeout. In either
 * case, the last read value at @args is stored in @val. Must not
 * be called from atomic context if sleep_us or timeout_us are used.
 */
#define read_poll_timeout(op, val, cond, sleep_us, timeout_us, \
			  sleep_before_read, args...) \
	poll_timeout_us((val) = op(args), cond, sleep_us, timeout_us, sleep_before_read)

/**
 * read_poll_timeout_atomic - Periodically poll an address until a condition is
 *                            met or a timeout occurs
 * @op: accessor function (takes @args as its arguments)
 * @val: Variable to read the value into
 * @cond: Break condition (usually involving @val)
 * @delay_us: Time to udelay between reads in us (0 tight-loops). Please
 *            read udelay() function description for details and
 *            limitations.
 * @timeout_us: Timeout in us, 0 means never timeout
 * @delay_before_read: if it is true, delay @delay_us before read.
 * @args: arguments for @op poll
 *
 * This macro does not rely on timekeeping.  Hence it is safe to call even when
 * timekeeping is suspended, at the expense of an underestimation of wall clock
 * time, which is rather minimal with a non-zero delay_us.
 *
 * When available, you'll probably want to use one of the specialized
 * macros defined below rather than this macro directly.
 *
 * Returns: 0 on success and -ETIMEDOUT upon a timeout. In either
 * case, the last read value at @args is stored in @val.
 */
#define read_poll_timeout_atomic(op, val, cond, sleep_us, timeout_us, \
				 sleep_before_read, args...) \
	poll_timeout_us_atomic((val) = op(args), cond, sleep_us, timeout_us, sleep_before_read)

/**
 * readx_poll_timeout - Periodically poll an address until a condition is met or a timeout occurs
 * @op: accessor function (takes @addr as its only argument)
 * @addr: Address to poll
 * @val: Variable to read the value into
 * @cond: Break condition (usually involving @val)
 * @sleep_us: Maximum time to sleep between reads in us (0 tight-loops). Please
 *            read usleep_range() function description for details and
 *            limitations.
 * @timeout_us: Timeout in us, 0 means never timeout
 *
 * When available, you'll probably want to use one of the specialized
 * macros defined below rather than this macro directly.
 *
 * Returns: 0 on success and -ETIMEDOUT upon a timeout. In either
 * case, the last read value at @addr is stored in @val. Must not
 * be called from atomic context if sleep_us or timeout_us are used.
 */
#define readx_poll_timeout(op, addr, val, cond, sleep_us, timeout_us)	\
	read_poll_timeout(op, val, cond, sleep_us, timeout_us, false, addr)

/**
 * readx_poll_timeout_atomic - Periodically poll an address until a condition is met or a timeout occurs
 * @op: accessor function (takes @addr as its only argument)
 * @addr: Address to poll
 * @val: Variable to read the value into
 * @cond: Break condition (usually involving @val)
 * @delay_us: Time to udelay between reads in us (0 tight-loops). Please
 *            read udelay() function description for details and
 *            limitations.
 * @timeout_us: Timeout in us, 0 means never timeout
 *
 * When available, you'll probably want to use one of the specialized
 * macros defined below rather than this macro directly.
 *
 * Returns: 0 on success and -ETIMEDOUT upon a timeout. In either
 * case, the last read value at @addr is stored in @val.
 */
#define readx_poll_timeout_atomic(op, addr, val, cond, delay_us, timeout_us) \
	read_poll_timeout_atomic(op, val, cond, delay_us, timeout_us, false, addr)

#define readb_poll_timeout(addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout(readb, addr, val, cond, delay_us, timeout_us)

#define readb_poll_timeout_atomic(addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout_atomic(readb, addr, val, cond, delay_us, timeout_us)

#define readw_poll_timeout(addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout(readw, addr, val, cond, delay_us, timeout_us)

#define readw_poll_timeout_atomic(addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout_atomic(readw, addr, val, cond, delay_us, timeout_us)

#define readl_poll_timeout(addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout(readl, addr, val, cond, delay_us, timeout_us)

#define readl_poll_timeout_atomic(addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout_atomic(readl, addr, val, cond, delay_us, timeout_us)

#define readq_poll_timeout(addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout(readq, addr, val, cond, delay_us, timeout_us)

#define readq_poll_timeout_atomic(addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout_atomic(readq, addr, val, cond, delay_us, timeout_us)

#define readb_relaxed_poll_timeout(addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout(readb_relaxed, addr, val, cond, delay_us, timeout_us)

#define readb_relaxed_poll_timeout_atomic(addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout_atomic(readb_relaxed, addr, val, cond, delay_us, timeout_us)

#define readw_relaxed_poll_timeout(addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout(readw_relaxed, addr, val, cond, delay_us, timeout_us)

#define readw_relaxed_poll_timeout_atomic(addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout_atomic(readw_relaxed, addr, val, cond, delay_us, timeout_us)

#define readl_relaxed_poll_timeout(addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout(readl_relaxed, addr, val, cond, delay_us, timeout_us)

#define readl_relaxed_poll_timeout_atomic(addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout_atomic(readl_relaxed, addr, val, cond, delay_us, timeout_us)

#define readq_relaxed_poll_timeout(addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout(readq_relaxed, addr, val, cond, delay_us, timeout_us)

#define readq_relaxed_poll_timeout_atomic(addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout_atomic(readq_relaxed, addr, val, cond, delay_us, timeout_us)

#endif /* _LINUX_IOPOLL_H */
