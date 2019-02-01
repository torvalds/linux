/** @file
 * IPRT - errno.h wrapper.
 */

/*
 * Copyright (C) 2012-2019 Oracle Corporation
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

#ifndef IPRT_INCLUDED_errno_h
#define IPRT_INCLUDED_errno_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef IPRT_NO_CRT
# if defined(RT_OS_DARWIN) && defined(KERNEL)
#  include <sys/errno.h>
# elif defined(RT_OS_LINUX) && defined(__KERNEL__)
#  include <linux/errno.h>
# elif defined(RT_OS_FREEBSD) && defined(_KERNEL)
#  include <sys/errno.h>
# elif defined(RT_OS_NETBSD) && defined(_KERNEL)
#  include <sys/errno.h>
# else
#  include <errno.h>
# endif
#endif


/*
 * Supply missing errno values according to the current RT_OS_XXX definition.
 *
 * Note! These supplements are for making no-CRT mode, as well as making UNIXy
 *       code that makes used of odd errno defines internally, work smoothly.
 *
 * When adding more error codes, always check the following errno.h sources:
 *  - RT_OS_DARWIN:  http://fxr.watson.org/fxr/source/bsd/sys/errno.h?v=xnu-1699.24.8
 *  - RT_OS_FREEBSD: http://fxr.watson.org/fxr/source/sys/errno.h?v=DFBSD
 *  - RT_OS_NETBSD:  http://fxr.watson.org/fxr/source/sys/errno.h?v=NETBSD
 *  - RT_OS_OPENBSD: http://fxr.watson.org/fxr/source/sys/errno.h?v=OPENBSD
 *  - RT_OS_OS2:     http://svn.netlabs.org/libc/browser/trunk/libc/include/sys/errno.h
 *  - RT_OS_LINUX:   http://fxr.watson.org/fxr/source/include/asm-generic/errno.h?v=linux-2.6
 *  - RT_OS_SOLARIS: http://fxr.watson.org/fxr/source/common/sys/errno.h?v=OPENSOLARIS
 *  - RT_OS_WINDOWS: tools/win.x86/vcc/v8sp1/include/errno.h
 */

#if defined(RT_OS_DARWIN) \
 || defined(RT_OS_FREEBSD) \
 || defined(RT_OS_NETBSD) \
 || defined(RT_OS_OPENBSD) \
 || defined(RT_OS_OS2)
# define RT_ERRNO_OS_BSD
#endif
#ifdef RT_OS_SOLARIS
# define RT_ERRNO_OS_SYSV_HARDCORE /* ?? */
#endif

/* The relatively similar part. */
#ifndef EPERM
# define EPERM                  (1)
#endif
#ifndef ENOENT
# define ENOENT                 (2)
#endif
#ifndef ESRCH
# define ESRCH                  (3)
#endif
#ifndef EINTR
# define EINTR                  (4)
#endif
#ifndef EIO
# define EIO                    (5)
#endif
#ifndef ENXIO
# define ENXIO                  (6)
#endif
#ifndef E2BIG
# define E2BIG                  (7)
#endif
#ifndef ENOEXEC
# define ENOEXEC                (8)
#endif
#ifndef EBADF
# define EBADF                  (9)
#endif
#ifndef ECHILD
# define ECHILD                 (10)
#endif
#ifndef EAGAIN
# if defined(RT_ERRNO_OS_BSD)
#  define EAGAIN                (35)
# else
#  define EAGAIN                (11)
# endif
#endif
#ifndef EWOULDBLOCK
# define EWOULDBLOCK            EAGAIN
#endif
#ifndef EDEADLK
# if defined(RT_ERRNO_OS_BSD)
#  define EDEADLK               (11)
# elif defined(RT_OS_LINUX)
#  define EDEADLK               (35)
# elif defined(RT_OS_WINDOWS)
#  define EDEADLK               (36)
# else
#  define EDEADLK               (45)
# endif
#endif
#ifndef EDEADLOCK
# define EDEADLOCK              EDEADLK
#endif
#ifndef ENOMEM
# define ENOMEM                 (12)
#endif
#ifndef EACCES
# define EACCES                 (13)
#endif
#ifndef EFAULT
# define EFAULT                 (14)
#endif
#ifndef ENOTBLK
# define ENOTBLK                (15)
#endif
#ifndef EBUSY
# define EBUSY                  (16)
#endif
#ifndef EEXIST
# define EEXIST                 (17)
#endif
#ifndef EXDEV
# define EXDEV                  (18)
#endif
#ifndef ENODEV
# define ENODEV                 (19)
#endif
#ifndef ENOTDIR
# define ENOTDIR                (20)
#endif
#ifndef EISDIR
# define EISDIR                 (21)
#endif
#ifndef EINVAL
# define EINVAL                 (22)
#endif
#ifndef ENFILE
# define ENFILE                 (23)
#endif
#ifndef EMFILE
# define EMFILE                 (24)
#endif
#ifndef ENOTTY
# define ENOTTY                 (25)
#endif
#ifndef ETXTBSY
# define ETXTBSY                (26)
#endif
#ifndef EFBIG
# define EFBIG                  (27)
#endif
#ifndef ENOSPC
# define ENOSPC                 (28)
#endif
#ifndef ESPIPE
# define ESPIPE                 (29)
#endif
#ifndef EROFS
# define EROFS                  (30)
#endif
#ifndef EMLINK
# define EMLINK                 (31)
#endif
#ifndef EPIPE
# define EPIPE                  (32)
#endif
#ifndef EDOM
# define EDOM                   (33)
#endif
#ifndef ERANGE
# define ERANGE                 (34)
#endif

/* 35 - also EAGAIN on BSD and EDEADLK on Linux. */
#ifndef ENOMSG
# if defined(RT_OS_DARWIN)
#  define ENOMSG                (91)
# elif defined(RT_OS_FREEBSD)
#  define ENOMSG                (83)
# elif defined(RT_OS_LINUX)
#  define ENOMSG                (42)
# else
#  define ENOMSG                (35)
# endif
#endif

/* 36 - Also EDEADLK on Windows.  */
#ifndef EIDRM
# if defined(RT_OS_DARWIN)
#  define EIDRM                 (90)
# elif defined(RT_OS_FREEBSD) || defined(RT_OS_NETBSD)
#  define EIDRM                 (82)
# elif defined(RT_OS_OPENBSD)
#  define EIDRM                 (89)
# elif defined(RT_OS_LINUX)
#  define EIDRM                 (43)
# elif defined(RT_OS_WINDOWS)
#  define EIDRM                 (600)
# else
#  define EIDRM                 (36)
# endif
#endif
#ifndef EINPROGRESS
# if defined(RT_ERRNO_OS_BSD)
#  define EINPROGRESS           (36)
# elif defined(RT_OS_LINUX)
#  define EINPROGRESS           (115)
# else
#  define EINPROGRESS           (150)
# endif
#endif
#ifndef ENAMETOOLONG
# if defined(RT_ERRNO_OS_BSD)
#  define ENAMETOOLONG          (63)
# elif defined(RT_OS_LINUX)
#  define ENAMETOOLONG          (36)
# else
#  define ENAMETOOLONG          (78)
# endif
#endif

/* 37 */
#ifndef ECHRNG
# if defined(RT_ERRNO_OS_SYSV_HARDCORE)
#  define ECHRNG                (37)
# else
#  define ECHRNG                (599)
# endif
#endif
#ifndef ENOLCK
# if defined(RT_ERRNO_OS_BSD)
#  define ENOLCK                (77)
# elif defined(RT_OS_LINUX)
#  define ENOLCK                (37)
# else
#  define ENOLCK                (46)
# endif
#endif
#ifndef EALREADY
# if defined(RT_ERRNO_OS_BSD)
#  define EALREADY              (37)
# elif defined(RT_OS_LINUX)
#  define EALREADY              (114)
# else
#  define EALREADY              (149)
# endif
#endif

/** @todo errno constants {37..44}. */

/* 45 - also EDEADLK on Solaris, EL2NSYNC on Linux. */
#ifndef ENOTSUP
# if defined(RT_ERRNO_OS_BSD)
#  define ENOTSUP               (45)
# elif defined(RT_OS_LINUX)
#  define ENOTSUP               (95)
# else
#  define ENOTSUP               (48)
# endif
#endif
#ifndef EOPNOTSUPP
# if defined(RT_ERRNO_OS_BSD)
#  define EOPNOTSUPP            ENOTSUP
# elif defined(RT_OS_LINUX)
#  define EOPNOTSUPP            ENOTSUP
# else
#  define EOPNOTSUPP            (122)
# endif
#endif

/** @todo errno constants {46..74}. */

/* 75 - note that Solaris has constant with value 75. */
#ifndef EOVERFLOW
# if defined(RT_OS_OPENBSD)
#  define EOVERFLOW             (87)
# elif defined(RT_ERRNO_OS_BSD)
#  define EOVERFLOW             (84)
# elif defined(RT_OS_LINUX)
#  define EOVERFLOW             (75)
# else
#  define EOVERFLOW             (79)
# endif
#endif
#ifndef EPROGMISMATCH
# if defined(RT_ERRNO_OS_BSD)
#  define EPROGMISMATCH         (75)
# else
#  define EPROGMISMATCH         (598)
# endif
#endif

/** @todo errno constants {76..}. */


#endif /* !IPRT_INCLUDED_errno_h */
