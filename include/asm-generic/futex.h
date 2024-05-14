/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_FUTEX_H
#define _ASM_GENERIC_FUTEX_H

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/errno.h>

#ifndef futex_atomic_cmpxchg_inatomic
#ifndef CONFIG_SMP
/*
 * The following implementation only for uniprocessor machines.
 * It relies on preempt_disable() ensuring mutual exclusion.
 *
 */
#define futex_atomic_cmpxchg_inatomic(uval, uaddr, oldval, newval) \
	futex_atomic_cmpxchg_inatomic_local(uval, uaddr, oldval, newval)
#define arch_futex_atomic_op_inuser(op, oparg, oval, uaddr) \
	futex_atomic_op_inuser_local(op, oparg, oval, uaddr)
#endif /* CONFIG_SMP */
#endif

/**
 * futex_atomic_op_inuser_local() - Atomic arithmetic operation with constant
 *			  argument and comparison of the previous
 *			  futex value with another constant.
 *
 * @encoded_op:	encoded operation to execute
 * @uaddr:	pointer to user space address
 *
 * Return:
 * 0 - On success
 * -EFAULT - User access resulted in a page fault
 * -EAGAIN - Atomic operation was unable to complete due to contention
 * -ENOSYS - Operation not supported
 */
static inline int
futex_atomic_op_inuser_local(int op, u32 oparg, int *oval, u32 __user *uaddr)
{
	int oldval, ret;
	u32 tmp;

	preempt_disable();

	ret = -EFAULT;
	if (unlikely(get_user(oldval, uaddr) != 0))
		goto out_pagefault_enable;

	ret = 0;
	tmp = oldval;

	switch (op) {
	case FUTEX_OP_SET:
		tmp = oparg;
		break;
	case FUTEX_OP_ADD:
		tmp += oparg;
		break;
	case FUTEX_OP_OR:
		tmp |= oparg;
		break;
	case FUTEX_OP_ANDN:
		tmp &= ~oparg;
		break;
	case FUTEX_OP_XOR:
		tmp ^= oparg;
		break;
	default:
		ret = -ENOSYS;
	}

	if (ret == 0 && unlikely(put_user(tmp, uaddr) != 0))
		ret = -EFAULT;

out_pagefault_enable:
	preempt_enable();

	if (ret == 0)
		*oval = oldval;

	return ret;
}

/**
 * futex_atomic_cmpxchg_inatomic_local() - Compare and exchange the content of the
 *				uaddr with newval if the current value is
 *				oldval.
 * @uval:	pointer to store content of @uaddr
 * @uaddr:	pointer to user space address
 * @oldval:	old value
 * @newval:	new value to store to @uaddr
 *
 * Return:
 * 0 - On success
 * -EFAULT - User access resulted in a page fault
 * -EAGAIN - Atomic operation was unable to complete due to contention
 */
static inline int
futex_atomic_cmpxchg_inatomic_local(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval)
{
	u32 val;

	preempt_disable();
	if (unlikely(get_user(val, uaddr) != 0)) {
		preempt_enable();
		return -EFAULT;
	}

	if (val == oldval && unlikely(put_user(newval, uaddr) != 0)) {
		preempt_enable();
		return -EFAULT;
	}

	*uval = val;
	preempt_enable();

	return 0;
}

#endif
