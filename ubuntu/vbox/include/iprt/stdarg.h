/** @file
 * IPRT - stdarg.h wrapper.
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef ___iprt_stdarg_h
#define ___iprt_stdarg_h

#ifdef IPRT_NO_CRT
# include <iprt/types.h>
# include <iprt/nocrt/compiler/compiler.h>
#else
# include <iprt/cdefs.h>
# if defined(RT_OS_FREEBSD) && defined(_KERNEL)
#  include <machine/stdarg.h>
# elif defined(RT_OS_NETBSD) && defined(_KERNEL)
#  include <sys/stdarg.h>
# elif defined(RT_OS_SOLARIS) && defined(_KERNEL) && defined(__GNUC__)
#  include <stdarg.h>
#  if __GNUC__ >= 4 /* System headers refers to __builtin_stdarg_start. */
#   define __builtin_stdarg_start __builtin_va_start
#  endif
# else
#  include <stdarg.h>
# endif
#endif

/*
 * MSC doesn't implement va_copy.
 */
#ifndef va_copy
# define va_copy(dst, src) do { (dst) = (src); } while (0) /** @todo check AMD64 */
#endif

#endif

