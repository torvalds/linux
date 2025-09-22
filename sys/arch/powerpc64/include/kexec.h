/*	$OpenBSD: kexec.h,v 1.3 2020/07/18 10:23:44 kettenis Exp $	*/

/*
 * Copyright (c) 2019-2020 Visa Hankala
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#ifndef _MACHINE_KEXEC_H_
#define _MACHINE_KEXEC_H_

#include <sys/ioccom.h>

#define KEXEC_MAX_ARGS	8	/* maximum number of boot arguments */

struct kexec_args {
	char		*kimg;		/* kernel image buffer */
	size_t		klen;		/* size of kernel image */
	int		boothowto;
	u_char		bootduid[8];
};

#define KIOC_KEXEC		_IOW('K', 1, struct kexec_args)
#define KIOC_GETBOOTDUID	_IOR('K', 2, u_char[8])

#endif /* _MACHINE_KEXEC_H_ */
