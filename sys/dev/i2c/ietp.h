/* $OpenBSD: ietp.h,v 1.2 2024/05/13 01:15:50 jsg Exp $ */
/*
 * Elantech touchpad I2C driver
 *
 * Copyright (c) 2015, 2016 joshua stein <jcs@openbsd.org>
 * Copyright (c) 2020, 2022 Vladimir Kondratyev <wulf@FreeBSD.org>
 * Copyright (c) 2023 vladimir serbinenko <phcoder@gmail.com>
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

#include "ihidev.h" // For i2c_hid_desc

struct ietp_softc {
	struct device	sc_dev;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;
	void		*sc_ih;
	union {
		uint8_t	hid_desc_buf[sizeof(struct i2c_hid_desc)];
		struct i2c_hid_desc hid_desc;
	};

	u_int		sc_isize;
	u_char		*sc_ibuf;

	int		sc_refcnt;

	int		sc_dying;

	struct device   *sc_wsmousedev;

  	uint8_t			sc_buttons;

	uint8_t			report_id;
	size_t			report_len;

	uint16_t	product_id;
	uint16_t	ic_type;

	int32_t		pressure_base;
	uint16_t	max_x;
	uint16_t	max_y;
	uint16_t	trace_x;
	uint16_t	trace_y;
	uint16_t	res_x;		/* dots per mm */
	uint16_t	res_y;
	bool		hi_precision;
	bool		is_clickpad;
};

int ietp_ioctl(void *, u_long, caddr_t, int, struct proc *);
int ietp_enable(void *dev);
void ietp_disable(void *dev);
