/*	$OpenBSD: macobio.h,v 1.2 2012/12/10 16:32:13 mpi Exp $	*/
/*
 * Copyright (c) 2011 Martin Pieuchot <mp@nolizard.org>
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

#ifndef _MACOBIO_H_
#define _MACOBIO_H_

#define GPIO_DDR_INPUT	0x00
#define GPIO_DDR_OUTPUT	0x04

#define GPIO_DATA	0x01	/* Data */
#define GPIO_LEVEL	0x02	/* Pin level (RO) */

void		macobio_enable(int, u_int32_t);
void		macobio_disable(int, u_int32_t);
uint8_t		macobio_read(int);
void		macobio_write(int, uint8_t);


#endif /* _MACOBIO_H_ */
