/*	$OpenBSD: pcex.h,v 1.2 2014/06/18 12:26:11 aoyama Exp $	*/

/*
 * Copyright (c) 2014 Kenji Aoyama.
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

#ifndef _MACHINE_PCEX_H_
#define _MACHINE_PCEX_H_

/*
 * PC-9801 extension board slot support for LUNA-88K2
 */

/* The ioctl defines */

#define	PCEXSETLEVEL	_IOW('P', 1, int)	/* Set INT level */
#define	PCEXRESETLEVEL	_IOW('P', 2, int)	/* Reset INT level */
#define	PCEXWAITINT	_IOW('P', 3, int)	/* Wait for INT  */

#endif /* _MACHINE_PCEX_H_ */
