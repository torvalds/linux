/*-
 * Copyright (c) 2015 Michal Meloun
 * Copyright (c) 2016 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under
 * sponsorship from the FreeBSD Foundation.
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

#ifndef _DEV_UART_CPU_ACPI_H_
#define _DEV_UART_CPU_ACPI_H_

#include <sys/linker_set.h>

struct uart_class;

struct acpi_uart_compat_data {
	const char *cd_hid;
	struct uart_class *cd_class;

	uint16_t cd_port_subtype;
	int cd_regshft;
	int cd_regiowidth;
	int cd_rclk;
	int cd_quirks;
	const char *cd_desc;
};

/*
 * If your UART driver implements only uart_class and uses uart_cpu_acpi.c
 * for device instantiation, then use UART_ACPI_CLASS_AND_DEVICE for its
 * declaration
 */
SET_DECLARE(uart_acpi_class_and_device_set, struct acpi_uart_compat_data);
#define UART_ACPI_CLASS_AND_DEVICE(data)				\
	DATA_SET(uart_acpi_class_and_device_set, data)

/*
 * If your UART driver implements uart_class and custom device layer,
 * then use UART_ACPI_CLASS for its declaration
 */
SET_DECLARE(uart_acpi_class_set, struct acpi_uart_compat_data);
#define UART_ACPI_CLASS(data)				\
	DATA_SET(uart_acpi_class_set, data)

#endif /* _DEV_UART_CPU_ACPI_H_ */
