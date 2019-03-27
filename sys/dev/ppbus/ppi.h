/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Nicolas Souchu
 * All rights reserved.
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
 *
 * $FreeBSD$
 *
 */
#ifndef __PPI_H
#define	__PPI_H

#ifndef _KERNEL
# include <sys/types.h>
#endif
#include <sys/ioccom.h>

#define	PPIGDATA	_IOR('P', 10, u_int8_t)
#define	PPIGSTATUS	_IOR('P', 11, u_int8_t)
#define	PPIGCTRL	_IOR('P', 12, u_int8_t)
#define	PPIGEPPD	_IOR('P', 13, u_int8_t)
#define	PPIGECR		_IOR('P', 14, u_int8_t)
#define	PPIGFIFO	_IOR('P', 15, u_int8_t)

#define	PPISDATA	_IOW('P', 16, u_int8_t)
#define	PPISSTATUS	_IOW('P', 17, u_int8_t)
#define	PPISCTRL	_IOW('P', 18, u_int8_t)
#define	PPISEPPD	_IOW('P', 19, u_int8_t)
#define	PPISECR		_IOW('P', 20, u_int8_t)
#define	PPISFIFO	_IOW('P', 21, u_int8_t)

#define	PPIGEPPA	_IOR('P', 22, u_int8_t)
#define	PPISEPPA	_IOR('P', 23, u_int8_t)

#endif
