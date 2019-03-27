/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Marcel Moolenaar
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/vmparam.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>

#define	UART_TAG_BR	0
#define	UART_TAG_CH	1
#define	UART_TAG_DB	2
#define	UART_TAG_DT	3
#define	UART_TAG_IO	4
#define	UART_TAG_MM	5
#define	UART_TAG_PA	6
#define	UART_TAG_RS	7
#define	UART_TAG_SB	8
#define	UART_TAG_XO	9
#define	UART_TAG_BD	10

static struct uart_class *uart_classes[] = {
	&uart_ns8250_class,
	&uart_sab82532_class,
	&uart_z8530_class,
#if defined(__arm__)
	&uart_s3c2410_class,
#endif
};

static bus_addr_t
uart_parse_addr(const char **p)
{
	return (strtoul(*p, (char**)(uintptr_t)p, 0));
}

static struct uart_class *
uart_parse_class(struct uart_class *class, const char **p)
{
	struct uart_class *uc;
	const char *nm;
	size_t len;
	u_int i;

	for (i = 0; i < nitems(uart_classes); i++) {
		uc = uart_classes[i];
		nm = uart_getname(uc);
		if (nm == NULL || *nm == '\0')
			continue;
		len = strlen(nm);
		if (strncmp(nm, *p, len) == 0) {
			*p += len;
			return (uc);
		}
	}
	return (class);
}

static long
uart_parse_long(const char **p)
{
	return (strtol(*p, (char**)(uintptr_t)p, 0));
}

static int
uart_parse_parity(const char **p)
{
	if (!strncmp(*p, "even", 4)) {
		*p += 4;
		return UART_PARITY_EVEN;
	}
	if (!strncmp(*p, "mark", 4)) {
		*p += 4;
		return UART_PARITY_MARK;
	}
	if (!strncmp(*p, "none", 4)) {
		*p += 4;
		return UART_PARITY_NONE;
	}
	if (!strncmp(*p, "odd", 3)) {
		*p += 3;
		return UART_PARITY_ODD;
	}
	if (!strncmp(*p, "space", 5)) {
		*p += 5;
		return UART_PARITY_SPACE;
	}
	return (-1);
}

static int
uart_parse_tag(const char **p)
{
	int tag;

	if ((*p)[0] == 'b' && (*p)[1] == 'd') {
		tag = UART_TAG_BD;
		goto out;
	}
	if ((*p)[0] == 'b' && (*p)[1] == 'r') {
		tag = UART_TAG_BR;
		goto out;
	}
	if ((*p)[0] == 'c' && (*p)[1] == 'h') {
		tag = UART_TAG_CH;
		goto out;
	}
	if ((*p)[0] == 'd' && (*p)[1] == 'b') {
		tag = UART_TAG_DB;
		goto out;
	}
	if ((*p)[0] == 'd' && (*p)[1] == 't') {
		tag = UART_TAG_DT;
		goto out;
	}
	if ((*p)[0] == 'i' && (*p)[1] == 'o') {
		tag = UART_TAG_IO;
		goto out;
	}
	if ((*p)[0] == 'm' && (*p)[1] == 'm') {
		tag = UART_TAG_MM;
		goto out;
	}
	if ((*p)[0] == 'p' && (*p)[1] == 'a') {
		tag = UART_TAG_PA;
		goto out;
	}
	if ((*p)[0] == 'r' && (*p)[1] == 's') {
		tag = UART_TAG_RS;
		goto out;
	}
	if ((*p)[0] == 's' && (*p)[1] == 'b') {
		tag = UART_TAG_SB;
		goto out;
	}
	if ((*p)[0] == 'x' && (*p)[1] == 'o') {
		tag = UART_TAG_XO;
		goto out;
	}
	return (-1);

out:
	*p += 2;
	if ((*p)[0] != ':')
		return (-1);
	(*p)++;
	return (tag);
}

/*
 * Parse a device specification. The specification is a list of attributes
 * separated by commas. Each attribute is a tag-value pair with the tag and
 * value separated by a colon. Supported tags are:
 *
 *	bd = Busy Detect
 *	br = Baudrate
 *	ch = Channel
 *	db = Data bits
 *	dt = Device type
 *	io = I/O port address
 *	mm = Memory mapped I/O address
 *	pa = Parity
 *	rs = Register shift
 *	sb = Stopbits
 *	xo = Device clock (xtal oscillator)
 *
 * The io and mm tags are mutually exclusive.
 */

int
uart_getenv(int devtype, struct uart_devinfo *di, struct uart_class *class)
{
	const char *spec;
	char *cp;
	bus_addr_t addr = ~0U;
	int error;

	/*
	 * All uart_class references are weak. Make sure the default
	 * device class has been compiled-in.
	 */
	if (class == NULL)
		return (ENXIO);

	/*
	 * Check the environment variables "hw.uart.console" and
	 * "hw.uart.dbgport". These variables, when present, specify
	 * which UART port is to be used as serial console or debug
	 * port (resp).
	 */
	switch (devtype) {
	case UART_DEV_CONSOLE:
		cp = kern_getenv("hw.uart.console");
		break;
	case UART_DEV_DBGPORT:
		cp = kern_getenv("hw.uart.dbgport");
		break;
	default:
		cp = NULL;
		break;
	}

	if (cp == NULL)
		return (ENXIO);

	/* Set defaults. */
	di->bas.chan = 0;
	di->bas.regshft = 0;
	di->bas.rclk = 0;
	di->baudrate = 0;
	di->databits = 8;
	di->stopbits = 1;
	di->parity = UART_PARITY_NONE;

	/* Parse the attributes. */
	spec = cp;
	for (;;) {
		switch (uart_parse_tag(&spec)) {
		case UART_TAG_BD:
			di->bas.busy_detect = uart_parse_long(&spec);
			break;
		case UART_TAG_BR:
			di->baudrate = uart_parse_long(&spec);
			break;
		case UART_TAG_CH:
			di->bas.chan = uart_parse_long(&spec);
			break;
		case UART_TAG_DB:
			di->databits = uart_parse_long(&spec);
			break;
		case UART_TAG_DT:
			class = uart_parse_class(class, &spec);
			break;
		case UART_TAG_IO:
			di->bas.bst = uart_bus_space_io;
			addr = uart_parse_addr(&spec);
			break;
		case UART_TAG_MM:
			di->bas.bst = uart_bus_space_mem;
			addr = uart_parse_addr(&spec);
			break;
		case UART_TAG_PA:
			di->parity = uart_parse_parity(&spec);
			break;
		case UART_TAG_RS:
			di->bas.regshft = uart_parse_long(&spec);
			break;
		case UART_TAG_SB:
			di->stopbits = uart_parse_long(&spec);
			break;
		case UART_TAG_XO:
			di->bas.rclk = uart_parse_long(&spec);
			break;
		default:
			freeenv(cp);
			return (EINVAL);
		}
		if (*spec == '\0')
			break;
		if (*spec != ',') {
			freeenv(cp);
			return (EINVAL);
		}
		spec++;
	}
	freeenv(cp);

	/*
	 * If we still have an invalid address, the specification must be
	 * missing an I/O port or memory address. We don't like that.
	 */
	if (addr == ~0U)
		return (EINVAL);

	/*
	 * Accept only the well-known baudrates. Any invalid baudrate
	 * is silently replaced with a 0-valued baudrate. The 0 baudrate
	 * has special meaning. It means that we're not supposed to
	 * program the baudrate and simply communicate with whatever
	 * speed the hardware is currently programmed for.
	 */
	if (di->baudrate >= 19200) {
		if (di->baudrate % 19200)
			di->baudrate = 0;
	} else if (di->baudrate >= 1200) {
		if (di->baudrate % 1200)
			di->baudrate = 0;
	} else if (di->baudrate > 0) {
		if (di->baudrate % 75)
			di->baudrate = 0;
	} else
		di->baudrate = 0;

	/* Set the ops and create a bus space handle. */
	di->ops = uart_getops(class);
	error = bus_space_map(di->bas.bst, addr, uart_getrange(class), 0,
	    &di->bas.bsh);
	return (error);
}
