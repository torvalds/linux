/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Neel Natu <neel@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
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

#ifndef _LPC_H_
#define	_LPC_H_

#include <sys/linker_set.h>

typedef void (*lpc_write_dsdt_t)(void);

struct lpc_dsdt {
	lpc_write_dsdt_t handler;
};

#define	LPC_DSDT(handler)						\
	static struct lpc_dsdt __CONCAT(__lpc_dsdt, __LINE__) = {	\
		(handler),						\
	};								\
	DATA_SET(lpc_dsdt_set, __CONCAT(__lpc_dsdt, __LINE__))

enum lpc_sysres_type {
	LPC_SYSRES_IO,
	LPC_SYSRES_MEM
};

struct lpc_sysres {
	enum lpc_sysres_type type;
	uint32_t base;
	uint32_t length;
};

#define	LPC_SYSRES(type, base, length)					\
	static struct lpc_sysres __CONCAT(__lpc_sysres, __LINE__) = {	\
		(type),							\
		(base),							\
		(length)						\
	};								\
	DATA_SET(lpc_sysres_set, __CONCAT(__lpc_sysres, __LINE__))

#define	SYSRES_IO(base, length)		LPC_SYSRES(LPC_SYSRES_IO, base, length)
#define	SYSRES_MEM(base, length)	LPC_SYSRES(LPC_SYSRES_MEM, base, length)

int	lpc_device_parse(const char *opt);
void    lpc_print_supported_devices();
char	*lpc_pirq_name(int pin);
void	lpc_pirq_routed(void);
const char *lpc_bootrom(void);

#endif
