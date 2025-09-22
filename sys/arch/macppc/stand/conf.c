/*	$OpenBSD: conf.c,v 1.16 2024/11/05 14:49:52 miod Exp $	*/
/*
 * Copyright (c) 2007 Dale Rahn <drahn@openbsd.org>
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
#include <sys/param.h>
  
#include <dev/cons.h>
     
#include <lib/libsa/stand.h>


const char version[] = "1.13";
extern int     debug;

void ofc_probe(struct consdev *);
void ofc_init(struct consdev *);
int ofc_getc(dev_t);
void ofc_putc(dev_t, int);


struct consdev *cn_tab;

struct consdev constab[] = {
	{ ofc_probe, ofc_init, ofc_getc, ofc_putc },
	{ NULL }
};
