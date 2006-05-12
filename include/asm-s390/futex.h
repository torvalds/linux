#ifndef _ASM_S390_FUTEX_H
#define _ASM_S390_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <asm/errno.h>
#include <asm/uaccess.h>

#ifndef __s390x__
#define __futex_atomic_fixup \
		     ".section __ex_table,\"a\"\n"			\
		     "   .align 4\n"					\
		     "   .long  0b,2b,1b,2b\n"				\
		     ".previous"
#else /* __s390x__ */
#define __futex_atomic_fixup \
		     ".section __ex_table,\"a\"\n"			\
		     "   .align 8\n"					\
		     "   .quad  0b,2b,1b,2b\n"				\
		     ".previous"
#endif /* __s390x__ */

#define __futex_atomic_op(insn, ret, oldval, newval, uaddr, oparg)	\
	asm volatile("   l   %1,0(%6)\n"				\
		     "0: " insn						\
		     "   cs  %1,%2,0(%6)\n"				\
		     "1: jl  0b\n"					\
		     "   lhi %0,0\n"					\
		     "2:\n"						\
		     __futex_atomic_fixup				\
		     : "=d" (ret), "=&d" (oldval), "=&d" (newval),	\
		       "=m" (*uaddr)					\
		     : "0" (-EFAULT), "d" (oparg), "a" (uaddr),		\
		       "m" (*uaddr) : "cc" );

static inline int futex_atomic_op_inuser (int encoded_op, int __user *uaddr)
{
	int op = (encoded_op >> 28) & 7;
	int cmp = (encoded_op >> 24) & 15;
	int oparg = (encoded_op << 8) >> 20;
	int cmparg = (encoded_op << 20) >> 20;
	int oldval = 0, newval, ret;
	if (encoded_op & (FUTEX_OP_OPARG_SHIFT << 28))
		oparg = 1 << oparg;

	if (! access_ok (VERIFY_WRITE, uaddr, sizeof(int)))
		return -EFAULT;

	inc_preempt_count();

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("lr %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op("lr %2,%1\nar %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("lr %2,%1\nor %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("lr %2,%1\nnr %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("lr %2,%1\nxr %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
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
	int ret;

	if (! access_ok (VERIFY_WRITE, uaddr, sizeof(int)))
		return -EFAULT;
	asm volatile("   cs   %1,%4,0(%5)\n"
		     "0: lr   %0,%1\n"
		     "1:\n"
#ifndef __s390x__
		     ".section __ex_table,\"a\"\n"
		     "   .align 4\n"
		     "   .long  0b,1b\n"
		     ".previous"
#else /* __s390x__ */
		     ".section __ex_table,\"a\"\n"
		     "   .align 8\n"
		     "   .quad  0b,1b\n"
		     ".previous"
#endif /* __s390x__ */
		     : "=d" (ret), "+d" (oldval), "=m" (*uaddr)
		     : "0" (-EFAULT), "d" (newval), "a" (uaddr), "m" (*uaddr)
		     : "cc", "memory" );
	return oldval;
}

#endif /* __KERNEL__ */
#endif /* _ASM_S390_FUTEX_H */
