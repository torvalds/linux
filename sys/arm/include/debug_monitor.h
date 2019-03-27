/*-
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Semihalf under
 * the sponsorship of the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_DEBUG_MONITOR_H_
#define	_MACHINE_DEBUG_MONITOR_H_

#ifdef DDB

#include <machine/db_machdep.h>

enum dbg_access_t {
	HW_BREAKPOINT_X		= 0,
	HW_WATCHPOINT_R		= 1,
	HW_WATCHPOINT_W		= 2,
	HW_WATCHPOINT_RW	= HW_WATCHPOINT_R | HW_WATCHPOINT_W,
};

#if __ARM_ARCH >= 6
void dbg_monitor_init(void);
void dbg_monitor_init_secondary(void);
void dbg_show_watchpoint(void);
int dbg_setup_watchpoint(db_expr_t, db_expr_t, enum dbg_access_t);
int dbg_remove_watchpoint(db_expr_t, db_expr_t);
void dbg_resume_dbreg(void);
#else /* __ARM_ARCH >= 6 */
static __inline void
dbg_show_watchpoint(void)
{
}
static __inline int
dbg_setup_watchpoint(db_expr_t addr __unused, db_expr_t size __unused,
    enum dbg_access_t access __unused)
{
	return (ENXIO);
}
static __inline int
dbg_remove_watchpoint(db_expr_t addr __unused, db_expr_t size __unused)
{
	return (ENXIO);
}
static __inline void
dbg_monitor_init(void)
{
}
static __inline void
dbg_monitor_init_secondary(void)
{
}
static __inline void
dbg_resume_dbreg(void)
{
}
#endif /* __ARM_ARCH < 6 */

#else /* DDB */
static __inline void
dbg_monitor_init(void)
{
}
#endif

#endif /* _MACHINE_DEBUG_MONITOR_H_ */
