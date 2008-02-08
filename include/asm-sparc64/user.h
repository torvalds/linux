/* $Id: user.h,v 1.1 1996/12/26 14:22:44 davem Exp $
 * asm-sparc64/user.h: Core file definitions for the Sparc.
 *
 * Keep in sync with reg.h.  Actually, we could get rid of this
 * one, since we won't a.out core dump that much anyways - miguel.
 * Copyright (C) 1995 (davem@caip.rutgers.edu)
 */
#ifndef _SPARC64_USER_H
#define _SPARC64_USER_H

#include <linux/a.out.h>
struct sunos_regs {
	unsigned int psr, pc, npc, y;
	unsigned int regs[15];
};

struct sunos_fpqueue {
	unsigned int *addr;
	unsigned int inst;
};

struct sunos_fp {
	union {
		unsigned int regs[32];
		double reg_dbls[16];
	} fregs;
	unsigned int fsr;
	unsigned int flags;
	unsigned int extra;
	unsigned int fpq_count;
	struct sunos_fpqueue fpq[16];
};

struct sunos_fpu {
	struct sunos_fp fpstatus;
};

/* The SunOS core file header layout. */
struct user {
	unsigned int magic;
	unsigned int len;
	struct sunos_regs regs;
	struct exec uexec;
	int           signal;
	size_t        u_tsize; /* all of these in bytes! */
	size_t        u_dsize;
	size_t        u_ssize;
	char          u_comm[17];
	struct sunos_fpu fpu;
	unsigned int  sigcode;   /* Special sigcontext subcode, if any */
};

#define NBPG                   PAGE_SIZE /* XXX 4096 maybe? */
#define UPAGES                 1
#define HOST_TEXT_START_ADDR   (u.start_code)
#define HOST_DATA_START_ADDR   (u.start_data)
#define HOST_STACK_END_ADDR    (u.start_stack + u.u_ssize * NBPG)
#define SUNOS_CORE_MAGIC       0x080456

#endif /* !(_SPARC64_USER_H) */
