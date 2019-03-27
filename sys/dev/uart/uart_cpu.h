/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003, 2004 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_UART_CPU_H_
#define _DEV_UART_CPU_H_

#include <sys/kdb.h>
#include <sys/lock.h>
#include <sys/mutex.h>

struct uart_softc;

/*
 * Low-level operations for use by console and/or debug port support.
 */
struct uart_ops {
	int (*probe)(struct uart_bas *);
	void (*init)(struct uart_bas *, int, int, int, int);
	void (*term)(struct uart_bas *);
	void (*putc)(struct uart_bas *, int);
	int (*rxready)(struct uart_bas *);
	int (*getc)(struct uart_bas *, struct mtx *);
};

extern bus_space_tag_t uart_bus_space_io;
extern bus_space_tag_t uart_bus_space_mem;

/*
 * Console and debug port device info.
 */
struct uart_devinfo {
	SLIST_ENTRY(uart_devinfo) next;
	struct uart_ops *ops;
	struct uart_bas bas;
	int	baudrate;
	int	databits;
	int	stopbits;
	int	parity;
	int	type;
#define	UART_DEV_CONSOLE	0
#define	UART_DEV_DBGPORT	1
#define	UART_DEV_KEYBOARD	2
	int	(*attach)(struct uart_softc*);
	int	(*detach)(struct uart_softc*);
	void	*cookie;		/* Type dependent use. */
	struct mtx *hwmtx;
	struct uart_softc *sc;		/* valid only from start of attach */
};

int uart_cpu_eqres(struct uart_bas *, struct uart_bas *);
int uart_cpu_getdev(int, struct uart_devinfo *);

int uart_getenv(int, struct uart_devinfo *, struct uart_class *);
const char *uart_getname(struct uart_class *);
struct uart_ops *uart_getops(struct uart_class *);
int uart_getrange(struct uart_class *);
u_int uart_getregshift(struct uart_class *);
u_int uart_getregiowidth(struct uart_class *);

void uart_add_sysdev(struct uart_devinfo *);

/*
 * Operations for low-level access to the UART. Primarily for use
 * by console and debug port logic.
 */

static __inline void
uart_lock(struct mtx *hwmtx)
{
	if (!kdb_active && hwmtx != NULL)
		mtx_lock_spin(hwmtx);
}

static __inline void
uart_unlock(struct mtx *hwmtx)
{
	if (!kdb_active && hwmtx != NULL)
		mtx_unlock_spin(hwmtx);
}

static __inline int
uart_probe(struct uart_devinfo *di)
{
	int res;

	uart_lock(di->hwmtx);
	res = di->ops->probe(&di->bas);
	uart_unlock(di->hwmtx);
	return (res);
}

static __inline void
uart_init(struct uart_devinfo *di)
{
	uart_lock(di->hwmtx);
	di->ops->init(&di->bas, di->baudrate, di->databits, di->stopbits,
	    di->parity);
	uart_unlock(di->hwmtx);
}

static __inline void
uart_term(struct uart_devinfo *di)
{
	uart_lock(di->hwmtx);
	di->ops->term(&di->bas);
	uart_unlock(di->hwmtx);
}

static __inline void
uart_putc(struct uart_devinfo *di, int c)
{
	uart_lock(di->hwmtx);
	di->ops->putc(&di->bas, c);
	uart_unlock(di->hwmtx);
}

static __inline int
uart_rxready(struct uart_devinfo *di)
{
	int res;

	uart_lock(di->hwmtx);
	res = di->ops->rxready(&di->bas);
	uart_unlock(di->hwmtx);
	return (res);
}

static __inline int
uart_poll(struct uart_devinfo *di)
{
	int res;

	uart_lock(di->hwmtx);
	if (di->ops->rxready(&di->bas))
		res = di->ops->getc(&di->bas, NULL);
	else
		res = -1;
	uart_unlock(di->hwmtx);
	return (res);
}

static __inline int
uart_getc(struct uart_devinfo *di)
{

	return (di->ops->getc(&di->bas, di->hwmtx));
}

void uart_grab(struct uart_devinfo *di);
void uart_ungrab(struct uart_devinfo *di);

#endif /* _DEV_UART_CPU_H_ */
