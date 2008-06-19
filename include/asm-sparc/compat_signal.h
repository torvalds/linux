#ifndef _COMPAT_SIGNAL_H
#define _COMPAT_SIGNAL_H

#include <linux/compat.h>
#include <asm/signal.h>

#ifdef CONFIG_COMPAT
struct __new_sigaction32 {
	unsigned		sa_handler;
	unsigned int    	sa_flags;
	unsigned		sa_restorer;     /* not used by Linux/SPARC yet */
	compat_sigset_t 	sa_mask;
};

struct __old_sigaction32 {
	unsigned		sa_handler;
	compat_old_sigset_t  	sa_mask;
	unsigned int    	sa_flags;
	unsigned		sa_restorer;     /* not used by Linux/SPARC yet */
};

typedef struct sigaltstack32 {
	u32			ss_sp;
	int			ss_flags;
	compat_size_t		ss_size;
} stack_t32;
#endif

#endif /* !(_COMPAT_SIGNAL_H) */
