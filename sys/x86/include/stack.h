/*-
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 *
 * $FreeBSD$
 */

#ifndef _X86_STACK_H
#define	_X86_STACK_H

/*
 * Stack trace.
 */

#ifdef __i386__
struct i386_frame {
	struct i386_frame	*f_frame;
	u_int			f_retaddr;
	u_int			f_arg0;
};
#endif

#ifdef __amd64__
struct amd64_frame {
	struct amd64_frame	*f_frame;
	u_long			f_retaddr;
};

struct i386_frame {
	uint32_t		f_frame;
	uint32_t		f_retaddr;
	uint32_t		f_arg0;
};
#endif /* __amd64__ */

#ifdef _KERNEL
int	stack_nmi_handler(struct trapframe *);
#endif

#endif /* !_X86_STACK_H */
