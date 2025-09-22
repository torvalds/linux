/*	$OpenBSD: udlio.h,v 1.1 2009/09/21 07:41:18 mglocker Exp $ */

/*
 * Copyright (c) 2009 Marcus Glocker <mglocker@openbsd.org>
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

/*
 * udl IOCTLs.
 */
struct udl_ioctl_damage {
	int	x1; 
	int	x2;
	int	y1;
	int	y2;
#define UDLIO_STATUS_OK		0
#define UDLIO_STATUS_FAILED	1
	int	status;
};
#define UDLIO_DAMAGE		_IOWR('W', 128, struct udl_ioctl_damage)
