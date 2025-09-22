/*	$OpenBSD: sxipiovar.h,v 1.2 2023/10/13 15:41:25 kettenis Exp $	*/
/*
 * Copyright (c) 2013 Artturi Alm
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/gpio.h>

struct sxipio_func {
	const char *name;
	int mux;
};

struct sxipio_pin {
	const char *name;
	int port, pin;
	struct sxipio_func funcs[10];
};

#define SXIPIO_PORT_A	0
#define SXIPIO_PORT_B	1
#define SXIPIO_PORT_C	2
#define SXIPIO_PORT_D	3
#define SXIPIO_PORT_E	4
#define SXIPIO_PORT_F	5
#define SXIPIO_PORT_G	6
#define SXIPIO_PORT_H	7
#define SXIPIO_PORT_I	8
#define SXIPIO_PORT_L	0
#define SXIPIO_PORT_M	1
#define SXIPIO_PORT_N	2

#define SXIPIO_PIN(port, pin) \
	"P" #port #pin,  SXIPIO_PORT_ ## port, pin
