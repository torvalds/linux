/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003, Trent Nelson, <trent@arpa.com>.
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */

#ifndef _CONVTBL_H_
#define _CONVTBL_H_

#include <sys/types.h>
#include <stdint.h>

/*
 * Keep the order in the enum.
 */
enum scale {
	SC_BYTE,
	SC_KILOBYTE,
	SC_MEGABYTE,
	SC_GIGABYTE,
	SC_TERABYTE,
	SC_BIT,
	SC_KILOBIT,
	SC_MEGABIT,
	SC_GIGABIT,
	SC_TERABIT,
	SC_AUTO		/* KEEP THIS LAST */
};

extern	double		 convert(const uintmax_t, const int);
extern	const char	*get_helplist(void);
extern	int		 get_scale(const char *);
extern	const char	*get_string(const uintmax_t, const int);

#endif		/* ! _CONVTBL_H_ */
