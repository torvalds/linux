#ifndef _ASM_POWERPC_FUTEX_H
#define _ASM_POWERPC_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <asm/errno.h>
#include <asm/synch.h>
#include <asm/uaccess.h>
#include <asm/asm-compat.h>

#define __futex_atomic_op(insn, ret, oldval, uaddr, oparg) \
  __asm__ __volatile ( \
	LWSYNC_ON_SMP \
"1:	lwarx	%0,0,%2\n" \
	insn \
	PPC405_ERR77(0, %2) \
"2:	stwcx.	%1,0,%2\n" \
	"bne-	1b\n" \
	"li	%1,0\n" \
"3:	.section .fixup,\"ax\"\n" \
"4:	li	%1,%3\n" \
	"b	3b\n" \
	".previous\n" \
	".section __ex_table,\"a\"\n" \
	".align 3\n" \
	PPC_LONG "1b,4b,2b,4b\n" \
	".previous" \
	: "=&r" (oldval), "=&r" (ret) \
	: "b" (uaddr), "i" (-EFAULT), "1" (oparg) \
	: "cr0", "memory")

static inline int futex_atomic_op_inuser (int encoded_op, int __user *uaddr)
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
		__futex_atomic_op("", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op("add %1,%0,%1\n", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("or %1,%0,%1\n", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("andc %1,%0,%1\n", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("xor %1,%0,%1\n", ret, oldval, uaddr, oparg);
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

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_FUTEX_H */
