/* $Id: user.h,v 1.5 1998/02/23 01:49:22 rth Exp $
 * asm-sparc/user.h: Core file definitions for the Sparc.
 *
 * Keep in sync with reg.h.  Actually, we could get rid of this
 * one, since we won't a.out core dump that much anyways - miguel.
 * Copyright (C) 1995 (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_USER_H
#define _SPARC_USER_H

#include <asm/a.out.h>
struct sunos_regs {
	unsigned long psr, pc, npc, y;
	unsigned long regs[15];
};

struct sunos_fpqueue {
	unsigned long *addr;
	unsigned long inst;
};

struct sunos_fp {
	union {
		unsigned long regs[32];
		double reg_dbls[16];
	} fregs;
	unsigned long fsr;
	unsigned long flags;
	unsigned long extra;
	unsigned long fpq_count;
	struct sunos_fpqueue fpq[16];
};

struct sunos_fpu {
	struct sunos_fp fpstatus;
};

/* The SunOS core file header layout. */
struct user {
	unsigned long magic;
	unsigned long len;
	struct sunos_regs regs;
	struct exec uexec;
	int           signal;
	size_t        u_tsize; /* all of these in bytes! */
	size_t        u_dsize;
	size_t        u_ssize;
	char          u_comm[17];
	struct sunos_fpu fpu;
	unsigned long sigcode;   /* Special sigcontext subcode, if any */
};

#define NBPG                   0x2000
#define UPAGES                 1
#define HOST_TEXT_START_ADDR   (u.start_code)
#define HOST_DATA_START_ADDR   (u.uexec.a_data)
#define HOST_STACK_END_ADDR    (- u.u_ssize * NBPG)
#define SUNOS_CORE_MAGIC       0x080456

#endif /* !(_SPARC_USER_H) */
