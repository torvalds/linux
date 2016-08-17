/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_SDT_H
#define	_SYS_SDT_H

#ifndef _KERNEL

#define	ZFS_PROBE(a)			((void) 0)
#define	ZFS_PROBE1(a, c)		((void) 0)
#define	ZFS_PROBE2(a, c, e)		((void) 0)
#define	ZFS_PROBE3(a, c, e, g)		((void) 0)
#define	ZFS_PROBE4(a, c, e, g, i)	((void) 0)
#define	ZFS_SET_ERROR(err)		((void) 0)

#else

#if defined(HAVE_DECLARE_EVENT_CLASS)

#include <sys/trace.h>

/*
 * The set-error SDT probe is extra static, in that we declare its fake
 * function literally, rather than with the DTRACE_PROBE1() macro.  This is
 * necessary so that SET_ERROR() can evaluate to a value, which wouldn't
 * be possible if it required multiple statements (to declare the function
 * and then call it).
 *
 * SET_ERROR() uses the comma operator so that it can be used without much
 * additional code.  For example, "return (EINVAL);" becomes
 * "return (SET_ERROR(EINVAL));".  Note that the argument will be evaluated
 * twice, so it should not have side effects (e.g. something like:
 * "return (SET_ERROR(log_error(EINVAL, info)));" would log the error twice).
 */
#define	SET_ERROR(err) \
	(trace_zfs_set__error(__FILE__, __func__, __LINE__, err), err)

#else

#undef SET_ERROR
#define	SET_ERROR(err) (err)

#endif /* HAVE_DECLARE_EVENT_CLASS */

#endif /* _KERNEL */

#endif /* _SYS_SDT_H */
