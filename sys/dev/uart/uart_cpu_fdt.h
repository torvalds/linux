/*-
 * Copyright 2015 Michal Meloun
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
 */

#ifndef _DEV_UART_CPU_FDT_H_
#define _DEV_UART_CPU_FDT_H_

#include <sys/linker_set.h>

#include <dev/ofw/ofw_bus_subr.h>

/*
 * If your UART driver implements only uart_class and uses uart_cpu_fdt.c
 * for device instantiation, then use UART_FDT_CLASS_AND_DEVICE for its
 * declaration
 */
SET_DECLARE(uart_fdt_class_and_device_set, struct ofw_compat_data );
#define UART_FDT_CLASS_AND_DEVICE(data)				\
	DATA_SET(uart_fdt_class_and_device_set, data)

/*
 * If your UART driver implements uart_class and custom device layer,
 * then use UART_FDT_CLASS for its declaration
 */
SET_DECLARE(uart_fdt_class_set, struct ofw_compat_data );
#define UART_FDT_CLASS(data)				\
	DATA_SET(uart_fdt_class_set, data)

int uart_cpu_fdt_probe(struct uart_class **, bus_space_tag_t *,
    bus_space_handle_t *, int *, u_int *, u_int *, u_int *);
int uart_fdt_get_clock(phandle_t node, pcell_t *cell);
int uart_fdt_get_shift(phandle_t node, pcell_t *cell);
int uart_fdt_get_io_width(phandle_t node, pcell_t *cell);

#endif /* _DEV_UART_CPU_FDT_H_ */
