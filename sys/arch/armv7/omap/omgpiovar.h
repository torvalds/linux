/* $OpenBSD: omgpiovar.h,v 1.2 2013/11/20 13:32:40 rapha Exp $ */
/*
 * Copyright (c) 2007,2009 Dale Rahn <drahn@openbsd.org>
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

#ifndef OMGPIOVAR_H
#define OMGPIOVAR_H

#define OMGPIO_DIR_IN 	0
#define OMGPIO_DIR_OUT	1

unsigned int omgpio_get_function(unsigned int gpio, unsigned int fn);
void omgpio_set_function(unsigned int gpio, unsigned int fn);
unsigned int omgpio_get_bit(unsigned int gpio);
void omgpio_set_bit(unsigned int gpio);
void omgpio_clear_bit(unsigned int gpio);
void omgpio_set_dir(unsigned int gpio, unsigned int dir);

int omgpio_pin_read(void *arg, int pin);
void omgpio_pin_write(void *arg, int pin, int value);
void omgpio_pin_ctl(void *arg, int pin, int flags);

/* interrupts */
void omgpio_clear_intr(unsigned int gpio);
void omgpio_intr_mask(unsigned int gpio);
void omgpio_intr_unmask(unsigned int gpio);
void omgpio_intr_level(unsigned int gpio, unsigned int level);
void *omgpio_intr_establish(unsigned int gpio, int level, int spl,
    int (*func)(void *), void *arg, char *name);
void omgpio_intr_disestablish(void *cookie);

#endif /* OMGPIOVAR_H */
