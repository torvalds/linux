#ifndef _ASM_GENERIC_SIGINFO_H
#define _ASM_GENERIC_SIGINFO_H

#include <uapi/asm-generic/siginfo.h>

#define __SI_MASK	0xffff0000u
#define __SI_KILL	(0 << 16)
#define __SI_TIMER	(1 << 16)
#define __SI_POLL	(2 << 16)
#define __SI_FAULT	(3 << 16)
#define __SI_CHLD	(4 << 16)
#define __SI_RT		(5 << 16)
#define __SI_MESGQ	(6 << 16)
#define __SI_SYS	(7 << 16)
#define __SI_CODE(T,N)	((T) | ((N) & 0xffff))

struct siginfo;
void do_schedule_next_timer(struct siginfo *info);

#ifndef HAVE_ARCH_COPY_SIGINFO

#include <linux/string.h>

static inline void copy_siginfo(struct siginfo *to, struct siginfo *from)
{
	if (from->si_code < 0)
		memcpy(to, from, sizeof(*to));
	else
		/* _sigchld is currently the largest know union member */
		memcpy(to, from, __ARCH_SI_PREAMBLE_SIZE + sizeof(from->_sifields._sigchld));
}

#endif

extern int copy_siginfo_to_user(struct siginfo __user *to, struct siginfo *from);

#endif
