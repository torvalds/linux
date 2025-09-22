/*	$OpenBSD: rsbvar.h,v 1.3 2019/08/29 11:51:48 kettenis Exp $	*/
/*
 * Copyright (c) 2017 Mark Kettenis <kettenis@openbsd.org>
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

struct rsb_attach_args {
	void		*ra_cookie;
	uint16_t	ra_da;
	uint8_t		ra_rta;
	char		*ra_name;
	int		ra_node;
};

int	rsb_print(void *, const char *);

uint8_t	rsb_read_1(void *, uint8_t, uint8_t);
uint16_t rsb_read_2(void *, uint8_t, uint8_t);
void	rsb_write_1(void *, uint8_t, uint8_t, uint8_t);
void	rsb_write_2(void *, uint8_t, uint8_t, uint16_t);
