// SPDX-License-Identifier: GPL-2.0
#include <linux/futex.h>

#ifndef FUTEX_WAIT_BITSET
#define FUTEX_WAIT_BITSET	  9
#endif
#ifndef FUTEX_WAKE_BITSET
#define FUTEX_WAKE_BITSET	 10
#endif
#ifndef FUTEX_WAIT_REQUEUE_PI
#define FUTEX_WAIT_REQUEUE_PI	 11
#endif
#ifndef FUTEX_CMP_REQUEUE_PI
#define FUTEX_CMP_REQUEUE_PI	 12
#endif
#ifndef FUTEX_CLOCK_REALTIME
#define FUTEX_CLOCK_REALTIME	256
#endif

static size_t syscall_arg__scnprintf_futex_op(char *bf, size_t size, struct syscall_arg *arg)
{
	enum syscall_futex_args {
		SCF_UADDR   = (1 << 0),
		SCF_OP	    = (1 << 1),
		SCF_VAL	    = (1 << 2),
		SCF_TIMEOUT = (1 << 3),
		SCF_UADDR2  = (1 << 4),
		SCF_VAL3    = (1 << 5),
	};
	int op = arg->val;
	int cmd = op & FUTEX_CMD_MASK;
	size_t printed = 0;

	switch (cmd) {
#define	P_FUTEX_OP(n) case FUTEX_##n: printed = scnprintf(bf, size, #n);
	P_FUTEX_OP(WAIT);	    arg->mask |= SCF_VAL3|SCF_UADDR2;		  break;
	P_FUTEX_OP(WAKE);	    arg->mask |= SCF_VAL3|SCF_UADDR2|SCF_TIMEOUT; break;
	P_FUTEX_OP(FD);		    arg->mask |= SCF_VAL3|SCF_UADDR2|SCF_TIMEOUT; break;
	P_FUTEX_OP(REQUEUE);	    arg->mask |= SCF_VAL3|SCF_TIMEOUT;	          break;
	P_FUTEX_OP(CMP_REQUEUE);    arg->mask |= SCF_TIMEOUT;			  break;
	P_FUTEX_OP(CMP_REQUEUE_PI); arg->mask |= SCF_TIMEOUT;			  break;
	P_FUTEX_OP(WAKE_OP);							  break;
	P_FUTEX_OP(LOCK_PI);	    arg->mask |= SCF_VAL3|SCF_UADDR2|SCF_TIMEOUT; break;
	P_FUTEX_OP(UNLOCK_PI);	    arg->mask |= SCF_VAL3|SCF_UADDR2|SCF_TIMEOUT; break;
	P_FUTEX_OP(TRYLOCK_PI);	    arg->mask |= SCF_VAL3|SCF_UADDR2;		  break;
	P_FUTEX_OP(WAIT_BITSET);    arg->mask |= SCF_UADDR2;			  break;
	P_FUTEX_OP(WAKE_BITSET);    arg->mask |= SCF_UADDR2;			  break;
	P_FUTEX_OP(WAIT_REQUEUE_PI);						  break;
	default: printed = scnprintf(bf, size, "%#x", cmd);			  break;
	}

	if (op & FUTEX_PRIVATE_FLAG)
		printed += scnprintf(bf + printed, size - printed, "|PRIV");

	if (op & FUTEX_CLOCK_REALTIME)
		printed += scnprintf(bf + printed, size - printed, "|CLKRT");

	return printed;
}

#define SCA_FUTEX_OP  syscall_arg__scnprintf_futex_op
