/*
 * include/asm-v850/sigcontext.h -- Signal contexts
 *
 *  Copyright (C) 2001  NEC Corporation
 *  Copyright (C) 2001  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_SIGCONTEXT_H__
#define __V850_SIGCONTEXT_H__

#include <asm/ptrace.h>

struct sigcontext
{
	struct pt_regs 	regs;
	unsigned long	oldmask;
};

#endif /* __V850_SIGCONTEXT_H__ */
