#ifndef _ASM_FUTEX_H
#define _ASM_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <asm/errno.h>
#include <asm/uaccess.h>

#ifdef CONFIG_SMP
#define __FUTEX_SMP_SYNC "	sync					\n"
#else
#define __FUTEX_SMP_SYNC
#endif

#define __futex_atomic_op(insn, ret, oldval, uaddr, oparg)		\
{									\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	noat					\n"	\
	"	.set	mips3					\n"	\
	"1:	ll	%1, (%3)	# __futex_atomic_op1	\n"	\
	"	.set	mips0					\n"	\
	"	" insn	"					\n"	\
	"	.set	mips3					\n"	\
	"2:	sc	$1, (%3)				\n"	\
	"	beqzl	$1, 1b					\n"	\
	__FUTEX_SMP_SYNC						\
	"3:							\n"	\
	"	.set	pop					\n"	\
	"	.set	mips0					\n"	\
	"	.section .fixup,\"ax\"				\n"	\
	"4:	li	%0, %5					\n"	\
	"	j	2b					\n"	\
	"	.previous					\n"	\
	"	.section __ex_table,\"a\"			\n"	\
	"	"__UA_ADDR "\t1b, 4b				\n"	\
	"	"__UA_ADDR "\t2b, 4b				\n"	\
	"	.previous					\n"	\
	: "=r" (ret), "=r" (oldval)					\
	: "0" (0), "r" (uaddr), "Jr" (oparg), "i" (-EFAULT));		\
}

static inline int
futex_atomic_op_inuser (int encoded_op, int __user *uaddr)
{
	int op = (encoded_op >> 28) & 7;
	int cmp = (encoded_op >> 24) & 15;
	int oparg = (encoded_op << 8) >> 20;
	int cmparg = (encoded_op << 20) >> 20;
	int oldval = 0, ret;
	if (encoded_op & (FUTEX_OP_OPARG_SHIFT << 28))
		oparg = 1 << oparg;

	if (! access_ok (VERIFY_WRITE, uaddr, sizeof(int)))
		return -EFAULT;

	inc_preempt_count();

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("move	$1, %z4", ret, oldval, uaddr, oparg);
		break;

	case FUTEX_OP_ADD:
		__futex_atomic_op("addu	$1, %1, %z4",
		                  ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("or	$1, %1, %z4",
		                  ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("and	$1, %1, %z4",
		                  ret, oldval, uaddr, ~oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("xor	$1, %1, %z4",
		                  ret, oldval, uaddr, oparg);
		break;
	default:
		ret = -ENOSYS;
	}

	dec_preempt_count();

	if (!ret) {
		switch (cmp) {
		case FUTEX_OP_CMP_EQ: ret = (oldval == cmparg); break;
		case FUTEX_OP_CMP_NE: ret = (oldval != cmparg); break;
		case FUTEX_OP_CMP_LT: ret = (oldval < cmparg); break;
		case FUTEX_OP_CMP_GE: ret = (oldval >= cmparg); break;
		case FUTEX_OP_CMP_LE: ret = (oldval <= cmparg); break;
		case FUTEX_OP_CMP_GT: ret = (oldval > cmparg); break;
		default: ret = -ENOSYS;
		}
	}
	return ret;
}

static inline int
futex_atomic_cmpxchg_inatomic(int __user *uaddr, int oldval, int newval)
{
	return -ENOSYS;
}

#endif
#endif
