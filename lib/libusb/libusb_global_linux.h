/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Hans Petter Selasky. All rights reserved.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _LIBUSB_GLOBAL_LINUX_H_
#define	_LIBUSB_GLOBAL_LINUX_H_

#define	_XOPEN_SOURCE
#define	_BSD_SOURCE
#ifdef __linux__
#define	_POSIX_SOURCE
#endif
#define	_POSIX_C_SOURCE 200809

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <alloca.h>
#endif
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <setjmp.h>
#include <pthread.h>
#include <sys/queue.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <dev/usb/usb_endian.h>
#include <dev/usb/usb_freebsd.h>

#include <compat/linux/linux_ioctl.h>

#define	IOUSB(a) FBSD_L##a

#ifndef __aligned
#define	__aligned(x) __attribute__((__aligned__(x)))
#endif

#ifndef __packed
#define	__packed __attribute__((__packed__))
#endif

#ifndef strlcpy
#define	strlcpy(d,s,len) do {			\
    strncpy(d,s,len);				\
    ((char *)d)[(len) - 1] = 0;			\
} while (0)
#endif

#endif					/* _LIBUSB_GLOBAL_LINUX_H_ */
